# esp32-air-sound

Home audio built on ESP32. Standalone R&D project, started 2026-09-27; not part of
smartest-home. The repository is private for now and **will be made public once the bench
work is done**, so everything committed here is written as if it were already public.

## What goes into this repository

Everything about the project: hardware choices and why, measurements, wiring, firmware,
what was tried and what failed. Record decisions in `docs/` as they are made, not later.
`docs/journal.md` is the dated log; `docs/decisions.md` holds the choices that stuck.

## What never goes into this repository

Nothing that ties the project to one particular home or person:

- Wi-Fi SSIDs, passphrases, MQTT/broker credentials, API keys, tokens, certificates.
- Addresses of devices on the home LAN (`10.x`, `192.168.x`), MAC addresses, hostnames of
  personal machines. Use placeholders (`<board-ip>`, `board.local`) in docs and logs.
- Personal e-mail beyond the committer identity `dev@skorin.online`; phone numbers; street
  addresses; names of other household members.
- Audio recordings or captures of any kind, including short test clips: the project is
  audio, so a stray `.wav` from a room is exactly the leak to avoid.
- Photos that show the interior of the home. Board-on-the-bench photos are fine.

`tools/check-private.sh` runs gitleaks with the project rules in `.gitleaks.toml` over the
tree and the full history; CI runs the same on every push. A finding is a blocker, not a
warning. If something private is committed anyway, rewriting history is the fix, not a
follow-up commit that deletes it.

Credentials live outside the tree (for example `~/.config/esp32-air-sound/*.env`) and are
provisioned onto the board at runtime, never compiled in.

## Conventions

- Documentation and commit messages in English, in the voice of the sibling repositories
  (`esp32-s3-eth-lab`, `esp32-s3-n16r8-bastion`): say what the thing does and why, record
  what cost time.
- Bump `version.txt`, the README badge and `CHANGELOG.md` together. Versions are firmware
  versions; while there is no firmware yet, they version the repository.
- Commit as `dev@skorin.online`. No `Co-Authored-By` lines.
- GitHub Actions pinned to commit SHAs with the version in a trailing comment.
- Tags `v*` trigger CI; there is no release job unless one is added deliberately.
