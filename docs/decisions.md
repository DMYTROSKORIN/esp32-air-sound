# Decisions

Choices that stuck, with the reasoning. A superseded decision stays here, struck through or
marked, so the path is visible.

| Date | Decision | Why |
|------|----------|-----|
| 2026-09-27 | Repository is public-ready from day one: no credentials, LAN/MAC addresses, personal data or recordings; enforced by gitleaks in CI. | The repository will be published after bench work; cleaning history later is costlier than never committing the data. |
| 2026-09-27 | Documentation and commits in English. | Consistent with the sibling ESP32 repositories and with the intended public audience. |
| 2026-09-27 | The device is an AirPlay (RAOP) receiver that re-broadcasts on FM, stereo with RDS. | Only protocol native to iOS, macOS and Fedora (PipeWire) at once; carries metadata for RDS and a DACP back channel for voice commands. Bluetooth impossible on the S3, Snapcast unusable from iOS. |
| 2026-09-27 | Microphone is for spoken next/stop/play only, after the MVP. | Owner's decision; keeps the audio path one-directional and the MVP small. |
| 2026-09-27 | Regulatory aspects of FM transmission are out of scope for this repository. | Owner's decision. |
| 2026-09-27 | smartest-home stays private; this repository carries only the MQTT contract the remote uses. | The two projects have different audiences. |
| 2026-09-27 | Bench is powered from USB; the LM2596 comes in at step 5. | One less thing to get wrong on the first assembly. |
| 2026-09-27 | Modules are powered from the devkit's `3V3` pin, not `5Vin`; the `IN-OUT` pads stay open. | `5Vin` is input-only on this devkit. 3.3 V is within both modules' range and puts the Si4713 module's I2C pull-ups at ESP32 logic level. |
| 2026-09-28 | AirPlay comes from squeezelite-esp32's RAOP component (MIT) plus Apple's ALAC decoder (Apache 2.0); the repository stays MIT, third-party parts listed in NOTICE. | Owner: reuse what works. The component's only GPL file was its log header, replaced. |
| 2026-09-28 | Setup, button and LED behaviour follow esp32-s3-n16r8-bastion; the ESP-IDF implementation comes from smartest-home's components. | Owner's choice; both are the owner's projects. |
| 2026-09-28 | A second AirPlay client takes over a session that has produced no sound for 5 s. | Owner: when nothing plays, any device on the network may play. PipeWire keeps silent sessions open; the stock receiver would block everyone else. |
| 2026-09-28 | RTP packets up to 2048 bytes; frames handed to the output ring half the latency ahead; 100 ms prefill. | Uncompressed ALAC from PipeWire is 1424 bytes a packet and arrives close to real time; measured zero jumps and zero underruns after the change. |
| 2026-09-28 | Digital trim 18 dB until the resistor divider exists. | 4 dB of headroom under the Si4713 limiter with pre-emphasis; the divider will move this attenuation into the analog domain. |
| 2026-09-28 | Default station name `Air-Sound`, RDS `AirSound`. | Owner's choice; the earlier name was an example. |
| 2026-09-28 | The resistor divider in the aux path is postponed to the enclosure build; the 18 dB digital trim stays until then. | Not audible over FM (the DAC keeps ~94 dB, the air carries ~60); the divider's real gain is noise margin on the cable, which matters once the wiring is final. Numbers in `audio-levels.md`. |
