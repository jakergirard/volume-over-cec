# Volume over CEC

Re-enables Apple TV volume control over HDMI-CEC on LG webOS TV speakers, with
the native on-screen volume bar.

LG webOS handles inbound CEC power, input, and navigation, but does not act on
inbound CEC `<User Control Pressed>` volume operands when audio is on the TV's
own speakers, and never surfaces them to the input layer (so no OSD). This runs a
small root daemon on a rooted TV that watches the CEC bus and, on Volume Up /
Down / Mute, injects the matching key event (`KEY_VOLUMEUP` 115, `KEY_VOLUMEDOWN`
114, `KEY_MUTE` 113) into an input node webOS already reads. That drives the
native volume path: real OSD, native repeat, same as the physical remote. Since
volume now rides CEC, the iOS Remote app controls it too, not just the physical
remote.

The injection method and keycodes follow the proven magic_mapper approach on
this platform: the Magic Remote is a normal evdev device whose volume buttons are
115/114, and a synthesized down/SYN/up/SYN `input_event` written to a node webOS
reads is honored by the system UI. A `--emit luna` fallback calls
`com.webos.service.audio` directly if injection isn't honored on your model; it
changes volume but shows no OSD.

## What's here

- `cecvold.c` - the daemon (also its own diagnostic and autodetect tool)
- `Makefile` - cross-compile to ARM
- `hooks/cecvold` - boot autostart hook (also copied to `app/cecvold.hook`)
- `app/` - Homebrew Channel app wrapper ("Volume over CEC" tile)
- `make_icons.py` - regenerates the app icons
- `cecvold.c.luna-bak` - the earlier audio-service-only version, kept for reference

## Prerequisites

- A rooted C1 with the Homebrew Channel. Check your model and firmware at
  `cani.rootmy.tv`; faultmanager covers webOS 6.
- SSH or Telnet to the TV as root.
- An ARM cross-compiler on your workstation.

## 1. Build

This TV is aarch64 (confirmed via `uname -m` on the TV). A prebuilt static
aarch64 binary, `cecvold`, is already included in this package, so you can skip
to step 2 and scp it. To build it yourself on macOS, pick one:

Docker (simplest; exact target arch, full kernel headers):

```
docker run --rm --platform linux/arm64 -v "$PWD":/src -w /src gcc:latest \
  gcc -O2 -static -o cecvold cecvold.c
```

Homebrew cross-toolchain (no Docker):

```
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
aarch64-unknown-linux-gnu-gcc -O2 -static -o cecvold cecvold.c
```

Either produces a static `cecvold`; verify with `file cecvold` (expect
"ELF 64-bit ... ARM aarch64 ... statically linked"). It uses `luna-send` (already
on the TV) only for the `--emit luna` fallback and autodetect; the inject path
has no runtime dependency. On a Linux box instead:
`make CC=aarch64-linux-gnu-gcc`.

## 2. Confirm the Apple TV emits CEC volume

The daemon can only act on frames the Apple TV actually sends.

- On the Apple TV: `Settings > Remotes and Devices > Volume Control > HDMI`.
- Copy the binary over and watch the bus:

```
scp cecvold root@TV_IP:/tmp/
ssh root@TV_IP
/tmp/cecvold -v
```

- Press Volume Up / Down on the Apple TV remote. A line like `rx len=3 44 41`
  is `<User Control Pressed>` Volume Up (`44` opcode; `41` Vol+, `42` Vol-, `43`
  Mute). If you see these, the source side works.
- `MONITOR_ALL unsupported` with no `44 xx` frames means the adapter can't see
  directed traffic; see Troubleshooting.
- Nothing at all means the Apple TV isn't emitting; see Troubleshooting.

## 3. Find the injection node

The node webOS reads for volume varies by model. Autodetect it:

```
/tmp/cecvold --autodetect
```

It probes `/dev/input/event0..31`, injects a `KEY_VOLUMEUP` into each, and prints
the node where master volume actually changes (restoring the level after). You'll
get a line like `FOUND: /dev/input/event4`. Note that path.

Optional manual check: `/tmp/cecvold --test-emit /dev/input/eventN` injects a
vol_up every 2 seconds so you can watch for the OSD, and
`/tmp/cecvold --sniff /dev/input/eventN` prints evdev key codes so you can
confirm the physical remote's volume codes (expect 115 / 114) and find the Magic
Remote node.

## 4. Install permanently

### Option A - as the Homebrew Channel app (recommended)

```
cp cecvold app/cecvold && chmod +x app/cecvold
npm i -g @webosose/ares-cli          # if you don't have ares-package
ares-package app/                    # -> org.webosbrew.cecvold_0.3.0_all.ipk
```

Install the ipk (Dev Manager Desktop is easiest: connect the TV, Install, pick
the ipk). Open "Volume over CEC" and follow the setup wizard: it checks the TV,

autodetects the injection node, and turns the bridge on. It writes the boot hook
at `/var/lib/webosbrew/init.d/cecvold` and starts on every reboot. After setup,
the app is a Status page with a live volume readout and an on/off toggle, plus Settings (on-screen display
vs fallback, node re-detect, hold behavior, keycodes) and Diagnostics (CEC volume
test, log).

If the wizard reports "service unreachable" (the hbchannel root-exec method name
varies by version), use Option B; it's equivalent.

### Option B - manual, over SSH (guaranteed)

```
# from your workstation:
scp cecvold        root@TV_IP:/media/developer/apps/usr/palm/applications/org.webosbrew.cecvold/cecvold
scp hooks/cecvold  root@TV_IP:/var/lib/webosbrew/init.d/cecvold

# on the TV:
chmod +x /media/developer/apps/usr/palm/applications/org.webosbrew.cecvold/cecvold
# put your autodetected node into the hook:
sed -i 's|^EMIT_NODE=.*|EMIT_NODE=/dev/input/event4|' /var/lib/webosbrew/init.d/cecvold
chmod +x /var/lib/webosbrew/init.d/cecvold
sh /var/lib/webosbrew/init.d/cecvold      # start now
```

(Create the app dir first with `mkdir -p` if you didn't install the ipk.)

## 5. Verify

```
pidof cecvold                 # a pid
cat /var/log/cecvold.log      # startup line + errors
```

Press volume from the Apple TV remote and from the iOS Remote app. The native
volume bar should appear and move in both cases.

## Behavior notes

- **Taps vs hold**: default emits one keystroke per CEC press and repeats taps
  every 150 ms while held (`REPEAT_MS`). Each tap shows the OSD and steps volume.
  If you'd rather use the TV's own repeat, add `--hold` (or uncomment that line
  in the hook): it holds the key down between CEC Pressed and Released. Try both
  and keep whichever feels right.
- **Latency**: injecting is one write, so press-to-change is basically native
  CEC timing (~100 ms of CEC wire time, same as any TV that supports this
  natively). The old audio-service path added a `luna-send` spawn per press; the
  inject path removes it.

## Troubleshooting

- **No `/dev/cec0`**: `ls /dev/cec*`; try `--cec /dev/cec1`. Ensure SimpLink is
  on and the Apple TV is on an HDMI input.
- **`--autodetect` finds nothing**: injection isn't changing volume on any node.
  Fall back to `--emit luna` (uncomment in the hook). Volume works, no OSD.
- **`MONITOR_ALL unsupported` and no frames**: the adapter can't monitor directed
  CEC. There's no input-layer workaround (the volume never reaches it, which is
  the whole bug). The supported alternative is an ARC soundbar/AVR on the eARC
  port with audio switched to it, which turns on System Audio Mode.
- **Apple TV emits nothing even in HDMI mode**: the HDMI hardware ACKs receipt so
  it shouldn't fall back, but some tvOS builds suppress CEC volume for TVs they've
  profiled. Toggle SimpLink off/on, re-pair the remote, reboot the Apple TV.
- **Physical remote uses different volume codes**: run `--sniff` on the Magic
  Remote node, then set `--key-up N --key-down N --key-mute N` to match.
- **Persistence across firmware updates**: root and the Homebrew Channel must
  survive. Block LG auto-updates, or re-root and reinstall after any update.

## Uninstall

```
rm -f /var/lib/webosbrew/init.d/cecvold
kill $(pidof cecvold) 2>/dev/null
```

Then remove the app from the launcher / Homebrew Channel if you installed the ipk.

## Offering it in the Homebrew Channel store

Installing the ipk is enough for your own TVs; it appears in the launcher and the
Homebrew Channel installed list. To make it browsable in the store, host a repo
manifest and add its URL under Homebrew Channel > Settings, or open a PR to the
webosbrew apps repository. The app id `org.webosbrew.cecvold` and the built ipk
are what you'd publish.
