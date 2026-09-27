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
- CI does not run yet: GitHub refuses to start hosted runners for this private repository
  ("recent account payments have failed or your spending limit needs to be increased").
  Until the repository is public, `tools/check-private.sh` is the gate; it runs the same
  gitleaks version as the workflow, through podman when the binary is not installed.

## 2026-09-27 — the picture is settled, wiring written

- Owner settled the design: a Wi-Fi audio output visible from iOS, macOS and Fedora, broadcast
  on FM in stereo with RDS track info, frequency set from the smartest-home remote, microphone
  only for next/stop/play and not in the MVP. Regulatory side out of scope.
- Protocol: AirPlay/RAOP, reasons in `roadmap.md`. Steps 0–5 there.
- `wiring.md`: the first assembly, USB-powered, 14 jumpers, one aux cable, one solder joint
  (antenna), with the two meter checks that decide whether it makes a sound.
- Owner confirmed: soldering iron, aux cable and antenna wire at hand; USB-C power for the
  whole first stage, standalone 5 V later; AirPlay accepted with Linux support required
  (PipeWire RAOP sink covers it). Bench diagram published as a private Artifact page for the
  owner; `wiring.md` corrected to match it (module headers in column `f`, wires in `g–j`,
  3V3 left unconnected until the microphone, whip antenna moved off the board).

## 2026-09-27 — bench photographed, wiring redone around it

- Owner placed both modules on the **left** breadboard, column `j`: Si4713 rows 30–40, PCM5102
  rows 58–63, aux cable already between the jacks. The previous plan (right board, right
  header) was thrown away rather than asking to move the boards.
- New pin allocation from the left header: RST 38, SCL 39, SDA 40, BCK 41, LCK 42, DIN 2;
  ground from the bottom GND of the left header; only +5 V crosses the devkit. GPIO39–42 are
  JTAG pins, free because the devkit debugs over USB Serial/JTAG; GPIO48 is the RGB LED.
- Convention for the bench: signals in column `i`, power in column `f`, so a hole name like
  `i13 → i30` is a complete instruction.
- Bench photo committed (no hands in frame).

## 2026-09-27 — bench wired and checked

- Owner wired everything per `wiring.md`, rang out all connections (no short between rails,
  rails continuous, power at every module pin, all six signals present and none crossed, CS
  open, DAC jumpers L L H L) and measured the rails with USB in: +5 V rail, Si4713 3Vo,
  devkit 3V3 all nominal. Nothing warm. Step 0 done.
- esptool sees the chip on `/dev/ttyACM0`: ESP32-S3 rev v0.2, 16 MB flash, 8 MB PSRAM.
- First firmware on the board: DAC and I2S come up, Si4713 does not answer, SDA reads low
  with the chip in reset and after a 9-clock recovery. Cause found by the owner reading the
  table against the board: the connection table skipped `CS` and had SCL, SDA, GND and VIN
  of the Si4713 one row too high (`i31`/`i32`/`f36`/`f37` instead of `i32`/`i33`/`f37`/`f38`).
  Wired that way, +5 V lands on the module's GND pin and 3Vo on the ground rail. Table,
  map and page corrected; pins now also given as "n-th from the top" so a row miscount
  cannot repeat this.

## 2026-09-27 — first carrier

- With the rows fixed the Si4713 still did not answer and both I2C lines read low. Meter on
  the module: VIN 0.75 V, 3Vo 0.23 V, SCL/SDA 1.4 V. The module had no power at all: on this
  devkit the `5Vin` header pin does **not** carry USB 5 V out unless the `IN-OUT` solder pads
  next to the RGB LED are bridged. The 1.4 V on the bus was the ESP32's pull-ups feeding an
  unpowered chip through its protection diodes, which the ESP32 reads as 0.
- Fix: the modules' `+` rail now comes from the devkit's `3V3` pin. Both modules accept it
  (Si4713 3–5 V, PCM5102 3.3–5.5 V) and the module's I2C pull-ups now sit at 3.3 V instead
  of 5 V, which is what the ESP32-S3 inputs want anyway. `IN-OUT` stays open.
- Result: `si4713 at 0x63: part 13, firmware 2.0, chip rev A`, on air at 100.00 MHz,
  105 dBuV, antenna cap 54 (13.5 pF), RDS PS loaded. The module's red LED lit for the first
  time (it is a power LED). Audio input still reads −69 dBfs: the DAC-to-transmitter path is
  the next thing to check.
