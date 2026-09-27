# Changelog

All notable changes to this project are recorded here. Versions are firmware versions.

## [Unreleased]

### Added
- AirPlay (RAOP) receiver: the station appears as an audio output on iOS, macOS and PipeWire;
  lossless ALAC in, 32-bit I2S out, track metadata to RDS radiotext, play/pause on BOOT.
- Setup over the phone: open network `AirSound-Setup`, captive page with network, station name,
  frequency, power and auto-update; the device joins the network before it saves anything.
- Signed A/B OTA from this repository's GitHub Releases, self-test with rollback, daily check.
- LED in the bastion language (green flash every 30 s on air, violet every 10 s while streaming),
  BOOT button; boot counter and reset reason in the journal.
- Two 6 MB OTA slots; Wi-Fi power save off while connected.

## [0.1.0] — 2026-09-27

### Added
- Step 1 firmware (ESP-IDF 5.5.3): I2C scan, Si4713 bring-up with tune, power, stereo
  pilot and RDS PS/radiotext, 1 kHz gated test tone to the PCM5102 over I2S, periodic input
  level and tune status report, GPIO-level bus diagnostic at start.
- Verified on the bench: chip found at 0x63, on air at 76.50 MHz, tone heard on a receiver in
  stereo, `AIRSOUND` shown by its RDS decoder.

### Fixed
- Wiring docs: Si4713 rows, module power from `3V3` (the devkit's `5Vin` is input-only),
  PCM5102 `H3L` jumper must be soldered on this batch.

## [0.0.1] — 2026-09-27

### Added
- Repository skeleton: README, licence, documentation layout, privacy rules and the
  secret-scanning workflow that has to pass before the repository is made public.
