# Changelog

Versions before 0.4.0 were unversioned development builds shared during bring-up
(protocol validation on hardware, two UI passes, and the on/off state fix). The
app is pre-1.0; 1.0.0 is reserved for the first release considered done.

## 1.0.0
- Universal ipk: ships both aarch64 (64-bit) and armv7l (32-bit) static builds
  of the daemon; `app/cecvold` is now a launcher that execs the one matching
  `uname -m`, so one package covers all rooted LG TVs.
- Process checks (hook and app UI) match the arch-specific binary names.

## 0.5.0
- Regenerated app icons: dark fill no longer bleeds outside the rounded
  corners, and the top sheen is a smooth gradient instead of a hard band.
  Added a 256px icon for the store listing.
- README rebuilt around the app (screenshots table, Mermaid how-it-works
  diagram); icon generator script removed - the final PNGs are committed.

## 0.4.0
- webOS-native UI: left rail, grouped rows, toggles, white focus highlight.
- Setup wizard reordered so the bridge is enabled before the Apple TV step,
  removing the app round-trip.
- "Switch to Apple TV" button (HDMI 1-4 picker) on the final wizard step and the
  Status page; launches the input directly. HDMI port persists in config.
- On/off is a flag in config, so turning off no longer drops back into setup.
- Fixed a wizard crash after the first step (stale DOM node).

## Earlier (unversioned dev builds)
- evdev key injection for the native on-screen volume bar (`--autodetect` for the
  injection node), with an audio-service fallback.
- Homebrew Channel app packaging (ipk), boot hook, and distribution kit.
- `cecvold` daemon: CEC monitor to volume, verified against Apple TV frames on an
  aarch64 C1.
