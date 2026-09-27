<p align="center">
  <h1 align="center">ESP32 Air Sound</h1>
  <p align="center"><strong>Home audio on an ESP32. Early bench work: the question of what exactly it should be is still being answered here, in the open.</strong></p>
</p>

<p align="center">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <a href="CHANGELOG.md"><img alt="Version" src="https://img.shields.io/badge/firmware-v0.2.0-2ea44f.svg"></a>
  <a href=".github/workflows/privacy.yml"><img alt="Privacy scan" src="https://img.shields.io/github/actions/workflow/status/DMYTROSKORIN/esp32-air-sound/privacy.yml?branch=main&label=privacy%20scan"></a>
  <img alt="MCU" src="https://img.shields.io/badge/mcu-ESP32-e7352c.svg">
  <img alt="Status" src="https://img.shields.io/badge/status-R%26D-orange.svg">
</p>

---

> [!CAUTION]
> Early R&D. Nothing here is a product yet. Expect the hardware list, the architecture and
> the firmware to change as the bench answers questions.

Where it stands (firmware 0.2.0): the board finds the Si4713, goes on air at 76.50 MHz with
RDS, and is an AirPlay output called `Air-Sound`. An iPhone and a Fedora laptop have played
through it with the track title on the radio's display; the stream is measured clean (no
dropped or torn packets, no buffer underruns). How to send from each system: `docs/senders.md`.
Next: the smartest-home remote over MQTT (`docs/roadmap.md`, step 3).

## What is this?

A home audio project with an ESP32-S3 at its core. On the bench today: an ESP32-S3-N16R8
devkit, a PCM5102A I2S DAC, an Si4713 FM transmitter, an INMP441 I2S microphone and an LM2596
buck converter, all described in `docs/hardware.md`. The device is an AirPlay receiver: pick it as the
audio output on an iPhone, a Mac or a Fedora laptop, and every FM radio in the flat tuned to
its frequency plays the music in stereo, with the track name on the RDS display. This repository is the whole project: the reasoning behind each
hardware choice, the measurements that backed it, the firmware, and a dated journal of what
was tried. It is being written as if it were already public, because it will be once the
bench work is done.

## Where things are

- `docs/journal.md` — dated log of the work, newest entry last.
- `docs/decisions.md` — choices that stuck and why; superseded ones stay, marked as such.
- `docs/hardware.md` — the boards on the bench, what each one is, and the ESP32 pins that are off-limits.
- `docs/wiring.md` — how to connect them, step by step, with the checks that matter.
- `docs/roadmap.md` — the whole picture, the protocol choice, and the steps to get there.
- `docs/senders.md` — how to play to it from iOS, macOS, Linux and Windows, and the rules for several senders.
- `docs/mqtt.md` — draft contract for the smartest-home remote (step 3), for the owner to review.
- `tools/check-private.sh` — the privacy scan that runs locally and in CI.
- `CLAUDE.md` — working rules for the repository, including what must never be committed.

## Privacy

The project is developed on a real home network and will be published. So the rules are
strict and enforced by CI on every push: no credentials, no home LAN or MAC addresses, no
personal contact details, no recordings. See `CLAUDE.md` for the full list and
`.gitleaks.toml` for what the scan looks for.

## License

MIT, see [LICENSE](LICENSE). Third-party parts (the RAOP receiver, Apple's ALAC decoder) and their
licences are listed in [NOTICE](NOTICE).
