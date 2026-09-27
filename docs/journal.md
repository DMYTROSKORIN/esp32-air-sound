# Journal

Dated log of the work. Newest entry last. Placeholders instead of real addresses:
`<board-ip>`, `<broker>`, `<ssid>`.

## 2026-09-27 — repository created

- Project started: home audio with an ESP32 at the core. Scope, board and topology are open.
- Repository is private during bench work and will be made public afterwards, so everything
  in it is written for a public reader from the first commit.
- Privacy rules written down in `CLAUDE.md`; gitleaks with project rules (`.gitleaks.toml`)
  runs locally via `tools/check-private.sh` and in CI over tree and history.

## 2026-09-27 — parts on the bench

- Identified from photos: ESP32-S3-N16R8 devkit, GY-PCM5102 I2S DAC, CJMCU-4713 (Si4713) FM
  transmitter, INMP441 I2S microphone, LM2596 buck with voltmeter. Details and the pins that
  are off-limits in `hardware.md`.
- No amplifier and no speaker among the parts, so the working reading is: the ESP32 sources
  audio, the DAC makes it line level, the Si4713 broadcasts it to the FM radios in the flat.
  The microphone's role is open.
- Two things to check before the first power-up: the four solder jumpers on the DAC (XSMT
  must be on the H side or the DAC is mute) and SCK on the DAC tied to GND.
- One thing to know before planning the source: the S3 has no Bluetooth Classic, so it cannot
  be an A2DP (Bluetooth speaker) sink.
- Module close-ups were not committed: taken in hand, fingerprints legible. Retake on the bench.
