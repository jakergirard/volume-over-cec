# Volume over CEC

Re-enables Apple TV volume control over HDMI-CEC on LG webOS TV speakers.

LG webOS doesn't act on inbound CEC volume for its own speakers, so an Apple TV
falls back to IR and the iOS Remote app can't change volume at all. This runs a
small root daemon on a rooted TV that catches CEC volume commands and applies
them to the TV.

## Build

Confirm the TV's userspace arch first (`uname -m` on the TV; expect `aarch64`):

```
sudo apt install gcc-aarch64-linux-gnu
make CC=aarch64-linux-gnu-gcc
```

## Install

Requires a rooted TV with the Homebrew Channel. Copy `cecvold` to the TV and
run it from a boot hook, or install the packaged app and press Enable.

On the Apple TV, set `Settings > Remotes and Devices > Volume Control > HDMI`.

Requires root. Not affiliated with LG or Apple.
