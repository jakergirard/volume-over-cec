# Changelog

The project is pre-1.0; 1.0.0 is reserved for the first release considered done.

## 0.3.0
- webOS-native UI: left rail, grouped rows, toggles, white focus highlight.
- Setup wizard (checks, node autodetect, enable), reordered so the bridge is on
  before the Apple TV step, removing the app round-trip.
- On/off is a flag in config, so turning off no longer drops back into setup.
- Fixed a wizard crash after the first step (stale DOM node).

## 0.2.0
- Native on-screen volume bar via evdev key injection (KEY_VOLUMEUP/DOWN/MUTE).
- `--autodetect` to find the injection node; `--emit luna` audio-service fallback.
- Distribution kit: release workflow and Homebrew Channel store manifest.

## 0.1.0
- Initial `cecvold` daemon: CEC monitor to `com.webos.service.audio` volume.
- Boot hook and a minimal on/off Homebrew Channel app.
