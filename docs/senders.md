# Sending music to the radio

The device is an AirPlay (RAOP, "AirPlay 1") receiver named after the station (`Air-Sound` by
default). Anything that can send AirPlay audio can play through it; the sound is lossless
44.1 kHz/16-bit on the way in and 32-bit on the way to the DAC.

| Sender | How | Notes |
|--------|-----|-------|
| iPhone, iPad | Control Centre → audio card → AirPlay → `Air-Sound` | Track title and artist go to RDS radiotext; the phone's volume slider sets the level. |
| Mac | Sound settings or the menu-bar volume control → `Air-Sound`; also from Music and Safari | Same protocol as iOS. |
| Fedora, other Linux with PipeWire | Enable RAOP discovery once: `~/.config/pipewire/pipewire.conf.d/raop-discover.conf` with `context.modules = [ { name = libpipewire-module-raop-discover } ]`, then pick `Air-Sound` in the **full** list of output devices (not the quick switcher) | PipeWire sends uncompressed ALAC in 1424-byte packets; this receiver was fixed to take them whole. |
| Windows | iTunes for Windows (the AirPlay button next to the volume slider) or a third-party sender such as TuneBlade or AirParrot | Windows has no system-wide AirPlay output of its own. |
| Anything else on the LAN | `owntone`, `shairport-sync` in sender mode, or `pw-play --target` from a script | The bench test uses `pw-play`. |

Rules the receiver applies, for the benefit of a household with several senders:

- One stream at a time, as with every AirPlay 1 speaker.
- A sender that has produced no sound for five seconds (paused, or holding the connection with
  silence, as PipeWire does) loses the receiver to the next device that asks. Whoever plays,
  plays; nobody parks the radio.
- The sender's volume is applied digitally, in 32-bit arithmetic, on top of a fixed trim that
  keeps the transmitter out of its limiter (`CONFIG_AIRSOUND_LINE_TRIM_DB`).
- While nothing streams the transmitter stays on air with silence and the station name, so a
  radio left tuned never falls into noise.
