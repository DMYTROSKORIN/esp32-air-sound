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
> A hobby build, not a product. Read the legal notice below before you switch anything on:
> FM transmission needs a licence almost everywhere, free frequency or not.

Where it stands (firmware 0.2.0): the board finds the Si4713, goes on air at 76.50 MHz with
RDS, and is an AirPlay output called `Air-Sound`. An iPhone and a Fedora laptop have played
through it with the track title on the radio's display; the stream is measured clean (no
dropped or torn packets, no buffer underruns). How to send from each system: `docs/senders.md`.
Next: the smartest-home remote over MQTT (`docs/roadmap.md`, step 3).

## What is this?

A DIY project, built at home on a breadboard for the fun of it: an ESP32-S3 that shows up as an
AirPlay speaker on a phone or a laptop and plays what it receives through a tiny FM transmitter,
so an ordinary FM radio in the next room becomes the loudspeaker, with the track title on its RDS
display. It is a way to learn a few things at once: I2S audio on the ESP32, the AirPlay 1
protocol, how an FM stereo transmitter with RDS is driven, and how much of an audio path can be
kept lossless on a microcontroller. It is not a product and it is not meant to become one.

This repository is the whole project: the reasoning behind each hardware choice, the
measurements that backed it, the firmware, and a dated journal of what was tried and what cost
time. It was written as if it were public from the first commit.

## Legal notice: read before you build this

**Transmitting on the FM broadcast band without a licence is illegal** in most countries, and a
frequency being free does not make it legal. Licence-free allowances, where they exist at all,
are tiny (a few tens of nanowatts) and far below what this transmitter produces even at its
lowest setting; the module used here is sold as a development board, not as a certified
transmitter. Whether and how you may operate this is your responsibility under the law where
you live.

This project exists for creative and educational purposes only. It must not be used for
commercial broadcasting, for rebroadcasting content you have no rights to, or for any
transmission that reaches beyond your own premises. The author publishes it as documentation of
a hobby build and accepts no liability for how it is used.

## Parts

Everything on the bench, and what each part does. Details, part numbers and the pins that are
off-limits on the devkit are in `docs/hardware.md`.

| Part | What it is | Role |
|------|------------|------|
| ESP32-S3-N16R8 devkit (44-pin, dual USB-C, WS2812 LED) | 16 MB flash, 8 MB PSRAM, Wi-Fi | Runs everything: AirPlay receiver, I2S out, I2C control, setup page, OTA |
| GY-PCM5102 (TI PCM5102A) | 32-bit I2S DAC, line out, no MCLK needed | Turns the stream into a line-level signal |
| CJMCU-4713 (Silicon Labs Si4713) | FM stereo transmitter with RDS, I2C | Puts the audio on the FM band; RDS carries the station name and the track |
| INMP441 | I2S MEMS microphone | Not wired yet: spoken next/stop/play is step 4 |
| LM2596 buck module | Adjustable step-down, 4–40 V in | Not wired yet: stand-alone 5 V supply is step 5 |
| 3.5 mm aux cable | | DAC output to transmitter input |
| ~75 cm of wire | | Antenna, in the `Ant` hole of the Si4713 |

No amplifier and no speaker: every FM radio in range is the speaker.

## How it is wired

The modules sit on a breadboard next to the devkit's left header; only power crosses the board.
Step-by-step with breadboard coordinates, the two meter checks and the mistakes already made:
`docs/wiring.md`.

| Signal | ESP32-S3 GPIO | Module pin |
|--------|---------------|------------|
| I2C SCL | 39 | Si4713 `SCL` |
| I2C SDA | 40 | Si4713 `SDA` |
| Transmitter reset | 38 | Si4713 `RST` |
| I2S bit clock | 41 | PCM5102 `BCK` |
| I2S word select | 42 | PCM5102 `LCK` |
| I2S data | 2 | PCM5102 `DIN` |
| 3.3 V | `3V3` | PCM5102 `VIN`, Si4713 `VIN` (both regulate on board) |
| Ground | `GND` | PCM5102 `GND`, PCM5102 `SCK` (no master clock: tie it low), Si4713 `GND` |
| Status LED | 48 | on the devkit |
| Button | 0 | `BOOT` on the devkit |
| Audio | PCM5102 jack | Si4713 jack, 3.5 mm cable |
| Antenna | | Si4713 `Ant`, ~75 cm wire |

Three things that cost an evening and are worth knowing before you copy this: the devkit's `5Vin`
pin does not carry USB power out unless its `IN-OUT` pads are bridged (hence 3.3 V for the
modules); the PCM5102 on this batch ships with all four solder jumpers open and is muted until
`H3L` is bridged to `H`; and the Si4713 `CS` pin stays open for I2C address `0x63`.

## Where things are

- `docs/hardware.md` — the boards on the bench, what each one is, and the ESP32 pins that are off-limits.
- `docs/wiring.md` — how to connect them, step by step, with the checks that matter.
- `docs/audio-levels.md` — DAC versus transmitter input levels, what the digital trim costs (nothing audible) and the divider for later.
- `docs/senders.md` — how to play to it from iOS, macOS, Linux and Windows, and the rules for several senders.
- `docs/roadmap.md` — the whole picture, the protocol choice, the steps, and the ideas not scheduled.
- `docs/mqtt.md` — draft contract for the smartest-home remote (step 3).
- `docs/journal.md` — dated log of the work, newest entry last.
- `docs/decisions.md` — choices that stuck and why; superseded ones stay, marked as such.
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
