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
