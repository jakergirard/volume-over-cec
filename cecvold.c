/*
 * cecvold - bridge inbound HDMI-CEC volume keys to the webOS UI
 *
 * LG webOS does not act on inbound CEC <User Control Pressed> volume operands
 * for the TV's own speakers, and it never surfaces them to the input layer
 * (which is why the volume OSD never appears). This daemon runs on a rooted TV,
 * watches the CEC bus in monitor mode, and on Volume Up / Down / Mute injects
 * the matching key event (KEY_VOLUMEUP/DOWN/MUTE) into an existing webOS input
 * node. That drives the native volume path: real on-screen OSD, native repeat,
 * same as the physical remote. Because volume now rides CEC, the iOS Remote app
 * controls it too, not just the physical remote.
 *
 * Injection method and keycodes follow the proven magic_mapper approach on this
 * platform: KEY_VOLUMEUP=115, KEY_VOLUMEDOWN=114, and a synthesized
 * down/SYN/up/SYN input_event written straight to a node webOS already reads.
 *
 * Emit fallback (--emit luna) calls com.webos.service.audio directly. That
 * changes volume but does not raise the OSD; use it only if injection isn't
 * honored on your model.
 *
 * Build (static, 32-bit ARM hard-float - typical LG webOS userspace):
 *   arm-linux-gnueabihf-gcc -O2 -static -o cecvold cecvold.c
 * Confirm target arch on the TV first: uname -m ; file /bin/sh
 *
 * Typical use:
 *   ./cecvold --autodetect              # find the injection node
 *   ./cecvold -v                        # watch CEC frames (set Apple TV to HDMI)
 *   ./cecvold --emit-node /dev/input/event4   # run for real
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

/* CEC uapi fallbacks - fixed by spec, safe against older headers */
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

/*
 * Explicit event layout using long for the time fields. This matches the
 * kernel's legacy input_event (16 bytes on 32-bit, 24 on 64-bit) and avoids the
 * glibc 64-bit time_t / struct timeval size change on armhf. Same width
 * magic_mapper writes with struct.pack("llHHi", ...).
 */
struct ievent { long sec; long usec; unsigned short type; unsigned short code; int value; };

enum { VOL_UP = 0, VOL_DOWN = 1, VOL_MUTE = 2 };
enum { EMIT_INJECT = 0, EMIT_LUNA = 1 };

static int g_key[3] = { KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_MUTE };  /* 115,114,113 */
static const char *g_emit_node = "/dev/input/event4";
static const char *g_output = "tv_speaker";     /* only used by --emit luna */
static int g_emit_mode = EMIT_INJECT;
static int g_hold = 0;          /* 1 = hold key down for native repeat */
static int g_verbose = 0;
static int g_emit_fd = -1;
static volatile sig_atomic_t g_stop = 0;

static const long REPEAT_MS = 150;      /* tap-repeat rate while a key is held */
static const long HOLD_WATCHDOG_MS = 800;

static void on_sig(int s) { (void)s; g_stop = 1; }

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void logmsg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); fflush(stderr);
    va_end(ap);
}

/* -------- evdev injection -------- */

static int emit_open(const char *path) {
    int fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) logmsg("open emit node %s: %s", path, strerror(errno));
    return fd;
}

static int emit_raw(int fd, unsigned short type, unsigned short code, int value) {
    struct ievent e;
    memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = value;   /* time left 0; kernel stamps */
    return (write(fd, &e, sizeof e) == (ssize_t)sizeof e) ? 0 : -1;
}

/* one down+SYN, or one up+SYN */
static int emit_edge(int fd, int keycode, int down) {
    if (emit_raw(fd, EV_KEY, keycode, down ? 1 : 0) < 0) return -1;
    return emit_raw(fd, EV_SYN, SYN_REPORT, 0);
}

/* full tap: down, SYN, up, SYN */
static int emit_tap(int fd, int keycode) {
    if (emit_edge(fd, keycode, 1) < 0) return -1;
    return emit_edge(fd, keycode, 0);
}

/* -------- luna fallback (no OSD) -------- */

static void luna(const char *method, const char *payload) {
    char cmd[512];
    snprintf(cmd, sizeof cmd,
             "luna-send -n 1 'luna://com.webos.service.audio/master/%s' '%s' "
             ">/dev/null 2>&1", method, payload);
    int rc = system(cmd); (void)rc;
}

static void luna_step(int which) {
    char p[128];
    if (which == VOL_MUTE) {
        int muted = 0;
        FILE *f = popen("luna-send -n 1 -f "
                        "'luna://com.webos.service.audio/master/getVolume' '{}' "
                        "2>/dev/null", "r");
        if (f) { char b[1024]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0;
                 pclose(f); if (strstr(b, "\"muted\":true")) muted = 1; }
        snprintf(p, sizeof p, "{\"soundOutput\":\"%s\",\"mute\":%s}",
                 g_output, muted ? "false" : "true");
        luna("muteVolume", p);
    } else {
        snprintf(p, sizeof p, "{\"soundOutput\":\"%s\"}", g_output);
        luna(which == VOL_UP ? "volumeUp" : "volumeDown", p);
    }
}

/* -------- unified emit -------- */

/* one discrete volume step (a tap / one luna call) */
static void emit_step(int which) {
    if (g_emit_mode == EMIT_LUNA) { luna_step(which); return; }
    if (g_emit_fd < 0) { g_emit_fd = emit_open(g_emit_node); if (g_emit_fd < 0) return; }
    if (emit_tap(g_emit_fd, g_key[which]) < 0) {
        close(g_emit_fd); g_emit_fd = -1;   /* reopen next time */
    }
}

/* hold key down (inject mode only) */
static void emit_hold(int which, int down) {
    if (g_emit_mode == EMIT_LUNA) { if (down) luna_step(which); return; }
    if (g_emit_fd < 0) { g_emit_fd = emit_open(g_emit_node); if (g_emit_fd < 0) return; }
    if (emit_edge(g_emit_fd, g_key[which], down) < 0) { close(g_emit_fd); g_emit_fd = -1; }
}

static int op_to_which(unsigned char ui) {
    if (ui == CEC_OP_UI_CMD_VOLUME_UP)   return VOL_UP;
    if (ui == CEC_OP_UI_CMD_VOLUME_DOWN) return VOL_DOWN;
    if (ui == CEC_OP_UI_CMD_MUTE)        return VOL_MUTE;
    return -1;
}

/* -------- CEC source -------- */

static int cec_open(const char *dev) {
    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { logmsg("open %s: %s", dev, strerror(errno)); return -1; }
    struct cec_caps caps; memset(&caps, 0, sizeof caps);
    if (ioctl(fd, CEC_ADAP_G_CAPS, &caps) == 0 && g_verbose)
        logmsg("adapter driver=%s name=%s caps=0x%x", caps.driver, caps.name, caps.capabilities);
    __u32 mode = CEC_MODE_MONITOR_ALL;
    if (ioctl(fd, CEC_S_MODE, &mode) < 0) {
        logmsg("MONITOR_ALL unsupported (%s); falling back to MONITOR", strerror(errno));
        mode = CEC_MODE_MONITOR;
        if (ioctl(fd, CEC_S_MODE, &mode) < 0) {
            logmsg("MONITOR failed: %s", strerror(errno)); close(fd); return -1;
        }
        logmsg("WARNING: MONITOR only - directed volume msgs may be invisible");
    }
    return fd;
}

static void run_cec(const char *dev) {
    int fd = -1;
    int held = -1;              /* which key is currently held (hold mode) */
    int repeating = 0;         /* tap-repeat active (non-hold mode) */
    int rkey = -1;
    long repeat_at = 0, watchdog_at = 0;

    while (!g_stop) {
        if (fd < 0) {
            fd = cec_open(dev);
            if (fd < 0) { sleep(2); continue; }
            logmsg("cecvold: monitoring %s, emit=%s node=%s hold=%d",
                   dev, g_emit_mode == EMIT_LUNA ? "luna" : "inject",
                   g_emit_node, g_hold);
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int timeout = (repeating || held >= 0) ? 60 : 1000;
        int pr = poll(&pfd, 1, timeout);
        if (pr < 0) { if (errno == EINTR) continue;
                      logmsg("poll: %s", strerror(errno)); close(fd); fd = -1; continue; }

        long t = now_ms();
        if (pr == 0) {                      /* timeout: drive repeat / watchdog */
            if (repeating && t >= repeat_at) { emit_step(rkey); repeat_at = t + REPEAT_MS; }
            if (held >= 0 && t >= watchdog_at) { emit_hold(held, 0); held = -1; }  /* safety release */
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) { close(fd); fd = -1; continue; }

        struct cec_msg msg; memset(&msg, 0, sizeof msg);
        if (ioctl(fd, CEC_RECEIVE, &msg) < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            logmsg("CEC_RECEIVE: %s", strerror(errno)); close(fd); fd = -1; continue;
        }
        if (msg.len < 2) continue;
        unsigned char op = msg.msg[1];

        if (g_verbose) {
            char hex[64] = {0};
            for (unsigned i = 0; i < msg.len && i < 18; i++)
                snprintf(hex + strlen(hex), sizeof hex - strlen(hex), "%02x ", msg.msg[i]);
            logmsg("rx len=%u %s", msg.len, hex);
        }

        if (op == CEC_MSG_USER_CONTROL_PRESSED && msg.len >= 3) {
            int which = op_to_which(msg.msg[2]);
            if (which < 0) continue;
            if (which == VOL_MUTE) { emit_step(VOL_MUTE); continue; }  /* mute never holds */

            if (g_hold) {
                if (held != which) { if (held >= 0) emit_hold(held, 0);
                                     emit_hold(which, 1); held = which; }
                watchdog_at = t + HOLD_WATCHDOG_MS;   /* refresh on each Pressed keep-alive */
            } else {
                emit_step(which);                     /* one step now */
                repeating = 1; rkey = which; repeat_at = t + REPEAT_MS;
                watchdog_at = t + HOLD_WATCHDOG_MS;
            }
        } else if (op == CEC_MSG_USER_CONTROL_RELEASED) {
            if (held >= 0) { emit_hold(held, 0); held = -1; }
            repeating = 0; rkey = -1;
        }
    }
    if (held >= 0) emit_hold(held, 0);
    if (fd >= 0) close(fd);
    if (g_emit_fd >= 0) close(g_emit_fd);
}

/* -------- utilities: sniff, test-emit, autodetect -------- */

static void run_sniff(const char *node) {
    int fd = open(node, O_RDONLY);
    if (fd < 0) { logmsg("open %s: %s", node, strerror(errno)); return; }
    logmsg("sniffing %s - press remote buttons (Ctrl-C to stop)", node);
    struct ievent e;
    while (!g_stop) {
        ssize_t r = read(fd, &e, sizeof e);
        if (r != (ssize_t)sizeof e) { if (r < 0 && errno == EINTR) continue; break; }
        if (e.type == EV_KEY)
            logmsg("EV_KEY code=%u value=%d", e.code, e.value);
    }
    close(fd);
}

static void run_test_emit(const char *node) {
    int fd = emit_open(node);
    if (fd < 0) return;
    logmsg("injecting KEY_VOLUMEUP to %s every 2s - watch for the OSD (Ctrl-C to stop)", node);
    while (!g_stop) {
        if (emit_tap(fd, KEY_VOLUMEUP) < 0) { logmsg("write failed"); break; }
        logmsg("sent vol_up");
        sleep(2);
    }
    close(fd);
}

static int query_volume(void) {   /* returns 0-100, or -1 */
    FILE *f = popen("luna-send -n 1 -f "
                    "'luna://com.webos.service.audio/master/getVolume' '{}' "
                    "2>/dev/null", "r");
    if (!f) return -1;
    char b[1024]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; pclose(f);
    char *p = strstr(b, "\"volume\":");
    if (!p) return -1;
    return atoi(p + 9);
}

static void run_autodetect(void) {
    logmsg("autodetect: probing /dev/input/event0..31 for the volume injection node");
    logmsg("(volume will tick during the scan; it is restored when found)");
    for (int i = 0; i < 32 && !g_stop; i++) {
        char path[32]; snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_WRONLY | O_NONBLOCK);
        if (fd < 0) continue;
        int before = query_volume();
        if (before < 0) { close(fd); continue; }
        emit_tap(fd, KEY_VOLUMEUP);
        usleep(350 * 1000);
        int after = query_volume();
        if (after != before) {
            emit_tap(fd, KEY_VOLUMEDOWN);   /* restore */
            close(fd);
            logmsg("FOUND: %s  (volume %d -> %d)", path, before, after);
            printf("%s\n", path);
            return;
        }
        close(fd);
    }
    logmsg("no injection node found. Injection may not be honored on this model; "
           "use --emit luna instead (works, but no OSD).");
}

/* -------- main -------- */

static void usage(const char *a0) {
    fprintf(stderr,
      "usage: %s [options]\n"
      "  --cec /dev/cecN        CEC adapter to monitor (default /dev/cec0)\n"
      "  --emit-node PATH       input node to inject into (default /dev/input/event4)\n"
      "  --emit inject|luna     inject evdev keys (OSD) or call audio service (no OSD)\n"
      "  --hold                 hold key down for native repeat (default: repeat taps)\n"
      "  --output NAME          sound output for --emit luna (default tv_speaker)\n"
      "  --key-up N --key-down N --key-mute N   override keycodes (default 115/114/113)\n"
      "  --autodetect           find the injection node, print it, exit\n"
      "  --sniff NODE           print evdev key events from NODE (find codes)\n"
      "  --test-emit NODE       inject vol_up to NODE every 2s (find OSD node)\n"
      "  -v                     verbose (dump CEC frames)\n", a0);
}

int main(int argc, char **argv) {
    const char *cecdev = "/dev/cec0";
    const char *sniff = NULL, *testemit = NULL;
    int autodetect = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else if (!strcmp(argv[i], "--hold")) g_hold = 1;
        else if (!strcmp(argv[i], "--autodetect")) autodetect = 1;
        else if (!strcmp(argv[i], "--cec") && i + 1 < argc) cecdev = argv[++i];
        else if (!strcmp(argv[i], "--emit-node") && i + 1 < argc) g_emit_node = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc) g_output = argv[++i];
        else if (!strcmp(argv[i], "--sniff") && i + 1 < argc) sniff = argv[++i];
        else if (!strcmp(argv[i], "--test-emit") && i + 1 < argc) testemit = argv[++i];
        else if (!strcmp(argv[i], "--key-up") && i + 1 < argc) g_key[VOL_UP] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--key-down") && i + 1 < argc) g_key[VOL_DOWN] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--key-mute") && i + 1 < argc) g_key[VOL_MUTE] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--emit") && i + 1 < argc) {
            g_emit_mode = strcmp(argv[++i], "luna") == 0 ? EMIT_LUNA : EMIT_INJECT;
        } else { usage(argv[0]); return 2; }
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    if (autodetect)      run_autodetect();
    else if (sniff)      run_sniff(sniff);
    else if (testemit)   run_test_emit(testemit);
    else                 run_cec(cecdev);

    logmsg("cecvold: exit");
    return 0;
}
