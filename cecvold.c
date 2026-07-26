/*
 * cecvold - bridge inbound HDMI-CEC volume keys to webOS master volume
 *
 * LG webOS does not act on inbound CEC <User Control Pressed> volume operands
 * for the TV's own speakers. This daemon runs on a rooted TV, watches the CEC
 * bus in monitor mode, and when it sees Volume Up / Volume Down / Mute directed
 * at the TV it calls luna://com.webos.service.audio/master/{volumeUp,volumeDown,
 * muteVolume}. LG's own CEC stack keeps ignoring the frames; this catches the
 * same frames in parallel and acts on them. No LG binary is patched.
 *
 * Two capture paths:
 *   default   - /dev/cec0 in MONITOR_ALL mode (sees directed traffic)
 *   --input   - /dev/input/eventX evdev, reads KEY_VOLUMEUP/DOWN/MUTE
 *               (fallback if the CEC adapter has no MONITOR_ALL capability but
 *                LG still injects the presses as input events)
 *
 * Build (static, 32-bit ARM hard-float - typical LG webOS userspace):
 *   arm-linux-gnueabihf-gcc -O2 -static -o cecvold cecvold.c
 * Confirm the target arch on the TV first: uname -m ; file /bin/sh
 *
 * Run to test:   ./cecvold -v
 * Run for real:  ./cecvold --cec /dev/cec0 --output tv_speaker
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/cec.h>
#include <linux/input.h>

/* Fallbacks so this compiles against older kernel headers. Values are fixed
 * by the CEC spec / uapi and will not change. */
#ifndef CEC_MODE_MONITOR
#define CEC_MODE_MONITOR 0xe0
#endif
#ifndef CEC_MODE_MONITOR_ALL
#define CEC_MODE_MONITOR_ALL 0xf0
#endif
#ifndef CEC_MSG_USER_CONTROL_PRESSED
#define CEC_MSG_USER_CONTROL_PRESSED 0x44
#endif
#ifndef CEC_MSG_USER_CONTROL_RELEASED
#define CEC_MSG_USER_CONTROL_RELEASED 0x45
#endif
#ifndef CEC_OP_UI_CMD_VOLUME_UP
#define CEC_OP_UI_CMD_VOLUME_UP 0x41
#endif
#ifndef CEC_OP_UI_CMD_VOLUME_DOWN
#define CEC_OP_UI_CMD_VOLUME_DOWN 0x42
#endif
#ifndef CEC_OP_UI_CMD_MUTE
#define CEC_OP_UI_CMD_MUTE 0x43
#endif

static const char *g_output = "tv_speaker";
static int g_verbose = 0;
static volatile sig_atomic_t g_stop = 0;

static const long REPEAT_MS = 180;   /* volume step rate while a key is held */

static void on_sig(int s) { (void)s; g_stop = 1; }

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(ap);
}

/* Fire a master-volume Luna call by exec'ing luna-send. Running as root on a
 * rooted TV this reaches com.webos.service.audio on the private bus. */
static void luna(const char *method, const char *payload) {
    char cmd[512];
    snprintf(cmd, sizeof cmd,
             "luna-send -n 1 'luna://com.webos.service.audio/master/%s' '%s' "
             ">/dev/null 2>&1",
             method, payload);
    int rc = system(cmd);
    (void)rc;
}

static void vol_up(void) {
    char p[96];
    snprintf(p, sizeof p, "{\"soundOutput\":\"%s\"}", g_output);
    luna("volumeUp", p);
}

static void vol_down(void) {
    char p[96];
    snprintf(p, sizeof p, "{\"soundOutput\":\"%s\"}", g_output);
    luna("volumeDown", p);
}

/* Read current mute state, then set the opposite. Mute is infrequent so the
 * extra query is fine and keeps us in sync if mute was toggled elsewhere. */
static void mute_toggle(void) {
    int muted = 0;
    FILE *f = popen("luna-send -n 1 -f "
                    "'luna://com.webos.service.audio/master/getVolume' '{}' "
                    "2>/dev/null", "r");
    if (f) {
        char buf[1024];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        pclose(f);
        if (strstr(buf, "\"muted\":true")) muted = 1;
    }
    char p[128];
    snprintf(p, sizeof p, "{\"soundOutput\":\"%s\",\"mute\":%s}",
             g_output, muted ? "false" : "true");
    luna("muteVolume", p);
}

static void dispatch(unsigned char ui) {
    switch (ui) {
        case CEC_OP_UI_CMD_VOLUME_UP:   vol_up();   break;
        case CEC_OP_UI_CMD_VOLUME_DOWN: vol_down(); break;
        case CEC_OP_UI_CMD_MUTE:        mute_toggle(); break;
    }
}

static int cec_open(const char *dev) {
    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        logmsg("open %s: %s", dev, strerror(errno));
        return -1;
    }
    struct cec_caps caps;
    memset(&caps, 0, sizeof caps);
    if (ioctl(fd, CEC_ADAP_G_CAPS, &caps) == 0 && g_verbose)
        logmsg("adapter driver=%s name=%s caps=0x%x",
               caps.driver, caps.name, caps.capabilities);

    __u32 mode = CEC_MODE_MONITOR_ALL;
    if (ioctl(fd, CEC_S_MODE, &mode) < 0) {
        logmsg("MONITOR_ALL unsupported (%s); falling back to MONITOR",
               strerror(errno));
        mode = CEC_MODE_MONITOR;
        if (ioctl(fd, CEC_S_MODE, &mode) < 0) {
            logmsg("MONITOR failed: %s", strerror(errno));
            close(fd);
            return -1;
        }
        logmsg("WARNING: MONITOR only - directed volume msgs may be invisible; "
               "if -v shows no '44 41' frames, use the --input fallback");
    }
    return fd;
}

static void run_cec(const char *dev) {
    int fd = -1;
    int repeating = 0;
    unsigned char rkey = 0;
    long repeat_at = 0;

    while (!g_stop) {
        if (fd < 0) {
            fd = cec_open(dev);
            if (fd < 0) { sleep(2); continue; }
            logmsg("cecvold: monitoring %s, output=%s", dev, g_output);
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int timeout = repeating ? (int)REPEAT_MS : 1000;
        int pr = poll(&pfd, 1, timeout);

        if (pr < 0) {
            if (errno == EINTR) continue;
            logmsg("poll: %s", strerror(errno));
            close(fd); fd = -1; continue;
        }
        if (pr == 0) {                 /* timeout: drive held-key repeat */
            if (repeating && now_ms() >= repeat_at) {
                dispatch(rkey);
                repeat_at = now_ms() + REPEAT_MS;
            }
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) { close(fd); fd = -1; continue; }

        struct cec_msg msg;
        memset(&msg, 0, sizeof msg);
        if (ioctl(fd, CEC_RECEIVE, &msg) < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            logmsg("CEC_RECEIVE: %s", strerror(errno));
            close(fd); fd = -1; continue;
        }
        if (msg.len < 2) continue;

        unsigned char op = msg.msg[1];
        if (g_verbose) {
            char hex[64] = {0};
            for (unsigned i = 0; i < msg.len && i < 18; i++)
                snprintf(hex + strlen(hex), sizeof hex - strlen(hex),
                         "%02x ", msg.msg[i]);
            logmsg("rx len=%u %s", msg.len, hex);
        }

        if (op == CEC_MSG_USER_CONTROL_PRESSED && msg.len >= 3) {
            unsigned char ui = msg.msg[2];
            if (ui == CEC_OP_UI_CMD_VOLUME_UP ||
                ui == CEC_OP_UI_CMD_VOLUME_DOWN ||
                ui == CEC_OP_UI_CMD_MUTE) {
                dispatch(ui);
                if (ui != CEC_OP_UI_CMD_MUTE) {   /* mute does not repeat */
                    repeating = 1;
                    rkey = ui;
                    repeat_at = now_ms() + REPEAT_MS;
                }
            }
        } else if (op == CEC_MSG_USER_CONTROL_RELEASED) {
            repeating = 0;
        }
    }
    if (fd >= 0) close(fd);
}

static void run_input(const char *dev) {
    int fd = -1;
    while (!g_stop) {
        if (fd < 0) {
            fd = open(dev, O_RDONLY);
            if (fd < 0) { logmsg("open %s: %s", dev, strerror(errno)); sleep(2); continue; }
            logmsg("cecvold: reading %s, output=%s", dev, g_output);
        }
        struct input_event ev;
        ssize_t r = read(fd, &ev, sizeof ev);
        if (r != (ssize_t)sizeof ev) {
            if (r < 0 && errno == EINTR) continue;
            close(fd); fd = -1; sleep(1); continue;
        }
        /* value 1 = press, 2 = autorepeat, 0 = release */
        if (ev.type == EV_KEY && ev.value != 0) {
            if (g_verbose) logmsg("evdev code=%u value=%d", ev.code, ev.value);
            if (ev.code == KEY_VOLUMEUP)        vol_up();
            else if (ev.code == KEY_VOLUMEDOWN) vol_down();
            else if (ev.code == KEY_MUTE && ev.value == 1) mute_toggle();
        }
    }
    if (fd >= 0) close(fd);
}

int main(int argc, char **argv) {
    const char *cecdev = "/dev/cec0";
    const char *inputdev = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else if (!strcmp(argv[i], "--cec") && i + 1 < argc) cecdev = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) inputdev = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc) g_output = argv[++i];
        else {
            fprintf(stderr,
                "usage: %s [-v] [--cec /dev/cecN] [--input /dev/input/eventX] "
                "[--output tv_speaker]\n", argv[0]);
            return 2;
        }
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    if (inputdev) run_input(inputdev);
    else          run_cec(cecdev);

    logmsg("cecvold: exit");
    return 0;
}
