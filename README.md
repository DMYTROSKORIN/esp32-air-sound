<p align="center">
  <h1 align="center">ESP32 Air Sound</h1>
  <p align="center"><strong>Home audio on an ESP32. Early bench work: the question of what exactly it should be is still being answered here, in the open.</strong></p>
</p>

<p align="center">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <a href="CHANGELOG.md"><img alt="Version" src="https://img.shields.io/badge/version-v0.0.1-2ea44f.svg"></a>
  <a href=".github/workflows/privacy.yml"><img alt="Privacy scan" src="https://img.shields.io/github/actions/workflow/status/DMYTROSKORIN/esp32-air-sound/privacy.yml?branch=main&label=privacy%20scan"></a>
  <img alt="MCU" src="https://img.shields.io/badge/mcu-ESP32-e7352c.svg">
  <img alt="Status" src="https://img.shields.io/badge/status-R%26D-orange.svg">
</p>

---

> [!CAUTION]
> Early R&D. Nothing here is a product yet. Expect the hardware list, the architecture and
> the firmware to change as the bench answers questions.

## What is this?

A home audio project with an ESP32 at its core. This repository is the whole project: the
reasoning behind each hardware choice, the measurements that backed it, the firmware, and a
dated journal of what was tried. It is being written as if it were already public, because it
will be once the bench work is done.

## Where things are

- `docs/journal.md` — dated log of the work, newest entry last.
- `docs/decisions.md` — choices that stuck and why; superseded ones stay, marked as such.
- `docs/hardware.md` — the boards, codecs, amplifiers and microphones under consideration or in use.
- `tools/check-private.sh` — the privacy scan that runs locally and in CI.
- `CLAUDE.md` — working rules for the repository, including what must never be committed.

## Privacy

The project is developed on a real home network and will be published. So the rules are
strict and enforced by CI on every push: no credentials, no home LAN or MAC addresses, no
personal contact details, no recordings. See `CLAUDE.md` for the full list and
`.gitleaks.toml` for what the scan looks for.

## License

MIT, see [LICENSE](LICENSE).
