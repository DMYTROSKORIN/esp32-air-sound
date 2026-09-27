# Roadmap

The whole picture, then the steps. Each step ends with something that can be heard on a radio.

## The device

A box on the home Wi-Fi that shows up on an iPhone, a Mac and a Fedora laptop as an audio
output. Pick it, play music, and every FM radio in range tuned to its frequency plays it, in
stereo, with the track name on the radio's RDS display. The frequency and the other settings
are changed from the smartest-home remote. Later, a microphone lets a spoken "next", "stop",
"play" control the source.

## Protocol choice: AirPlay (RAOP)

The one protocol all three senders already speak without installing anything: iOS and macOS
natively, Fedora through PipeWire's RAOP discover module. It delivers 44.1 kHz/16-bit stereo
(ALAC, lossless), carries track metadata and cover art for the RDS text, and has a back
channel (DACP) through which the receiver can tell the sender play/pause/next, which is
exactly what the voice commands need. AirPlay 1 receivers still appear on current iOS and
macOS. Snapcast (needs a server, iOS cannot send to it) and Bluetooth (the S3 has no
Bluetooth Classic) are out.

## Steps

| Step | Deliverable | Heard on the radio |
|------|-------------|--------------------|
| 0 | Bench wired per `wiring.md`, power checked | nothing yet |
| 1 | Firmware: I2C finds the Si4713, DAC plays a test tone, transmitter on air with RDS PS | a 1 kHz tone in stereo, station name on the display |
| 2 | AirPlay receiver → I2S → FM; metadata → RDS PS/RT. **Done 2026-09-28: iPhone and Fedora verified, zero jumps and underruns in the stream, track title in RDS; macOS and Windows senders documented in `senders.md`, owner's listening test pending** | music from the phone, track title scrolling |
| 3 | ~~Wi-Fi provisioning~~ (done with step 2: captive setup page, signed OTA), status page, frequency/power/RDS settings over MQTT; the smartest-home remote drives them | frequency changes from the remote |
| 4 | INMP441 + on-device keyword spotting for next/stop/play, sent back over DACP | the track skips when told to |
| 5 | LM2596 power from a wall supply, enclosure, antenna done properly | the same, without the laptop |

Step 2 is the MVP. Steps 1 and 2 need only what is on the bench today.

## Boundary with smartest-home

smartest-home is a private repository and stays private. This repository holds the device
and the *contract* it exposes: MQTT topics, payloads, discovery. Nothing about the hub's
internals, its addresses or its credentials appears here; the remote is described only as
"an MQTT client that sets these topics".
