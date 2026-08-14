# Changelog

Versions before 0.4.0 were unversioned development builds shared during bring-up
(protocol validation on hardware, two UI passes, and the on/off state fix). The
app is pre-1.0; 1.0.0 is reserved for the first release considered done.

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
