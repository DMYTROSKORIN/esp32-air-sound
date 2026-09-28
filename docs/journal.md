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

## 2026-09-27 — first sound on the air

- Carrier confirmed on a receiver 10 cm from the board, no antenna yet. Moved to 76.50 MHz
  (100.0 was taken); the Si4713 retuned its antenna cap from 54 to ~99 on its own.
- Transmitter input stayed at −65 dBfs with the DAC clocked and powered (BCK/LCK 1.6 V DC,
  DIN 0.8 V, VIN 3.3 V). Meter on the top-side hole `3` (XSMT) of the PCM5102: 0.2 V. Photo
  of the back: **none of the four solder jumpers is populated on this batch**, so XSMT floats
  low and the DAC is hardware-muted. FLT/DEMP/FMT floating give the wanted defaults.
- Fix: one solder blob on `H3L`, middle pad to `H`. Tone came through at once: receiver
  beeps, transmitter reports −2 dBfs OVERMOD with the bench settings (tone −1 dBFS, line
  input at 190 mV). Restored to line input 3 (636 mVpk) and tone −20 dBFS: −12 dBfs, clean.
- Step 1 is done: I2C finds the chip, DAC plays, transmitter carries it with RDS loaded.
  Not yet seen: `AIRSOUND` on the receiver display, which needs the antenna wire.
- RDS confirmed on the receiver (an XHDATA D-808, Si4735-based): with a wire on `Ant`
  touching the receiver's whip it showed `Stereo`, 59 dBµ, SNR 36 dB, and `AIRSOUND` once its
  RDS page was selected. On that radio `DISPLAY` cycles the top-right corner only; the RDS
  pages (PS, PTY, radiotext, date) are on `INFO`. The chip's own counters agree: components
  0x0007, PS and radiotext groups transmitting. Step 1 done end to end.

## 2026-09-28 — AirPlay: first stream

- Owner's decisions overnight: reuse rather than rewrite, GPL acceptable; copy the smartest-home
  components; setup, button and LED behaviour from bastion; no resistors in the house yet; the
  antenna wire is only pushed into `Ant` for now.
- Foundation ported from smartest-home in ESP-IDF 5.5: NVS configuration, RAM journal, Wi-Fi
  station with the captive setup page (now asks for station name, frequency, power and
  auto-update, and joins the network before writing anything), signed A/B OTA against this
  repository's GitHub Releases with a new Ed25519 key, boot counter and reset reason. LED and
  BOOT button follow bastion's language. Audio path rebuilt as one 32-bit I2S output with a
  two-second PSRAM ring and the line trim applied in 32-bit arithmetic. Two 6 MB OTA slots.
  Wi-Fi power save off once connected (ping went from 250 ms to a few ms).
- AirPlay: the RAOP component from squeezelite-esp32 turned out to be MIT (philippe44's
  AirConnect code), only its logging header was GPL; that header is replaced, Apple's ALAC
  decoder is compiled from source (Apache 2.0) behind a small wrapper, and the repository stays
  MIT (NOTICE lists the parts). Three things bit on the way: the tasks were created with a core
  id of -1, which the current FreeRTOS asserts on; Apple's endian header does not know Xtensa
  and read the config cookie backwards until `TARGET_RT_LITTLE_ENDIAN` was defined; and a flash
  I started in the background while another process opened the port left a half-written image
  ("invalid segment length") — flash from the foreground, alone on the port.
- Result: `Air-Sound` appears as an AirPlay output on Fedora (PipeWire's RAOP discover module);
  a test tone streamed for 28 s, the transmitter reported −20 dBfs at its input while it
  played and −63 dBfs afterwards. iOS and macOS, and the track title in RDS, still to be
  tried by the owner.
- iOS confirmed by the owner: an iPhone picked `Air-Sound` from its AirPlay list, the music came
  out of the radio on 76.5 and the D-808 showed the track as RDS radiotext. The board's log
  during the session: RECORD, volume −20 dB from the phone, metadata and artwork, DACP remote
  found, input at the transmitter −24…−34 dBfs with no overmodulation, ring about 190 ms.
  The phone's first attempt failed while the board was still rebooting after a flash.
- LED pattern changed at the owner's request: one green flash every 30 s when idle, one violet
  flash every 10 s while a stream plays.
- Fedora: PipeWire's `libpipewire-module-raop-discover` lists the station as an output device
  called `Air-Sound` (in the full device list, not the quick switcher); it is now loaded from
  `~/.config/pipewire/pipewire.conf.d/` so it survives a restart.
- Still to check by ear tomorrow: stereo, distortion at full phone volume (whether the resistor
  divider is needed now), dropouts.

## 2026-09-28 (night) — sound quality from every sender

- Owner's brief before bed: run the tests, the documentation and the diagrams; sound quality
  is priority one, from macOS, Linux and Windows alike. Device renamed `Air-Sound`
  (RDS `AirSound`); the station name is just a default, the portal changes it.
- Fedora streamed with "digital distortion, unlistenable" while the iPhone sounded fine. Measured
  rather than guessed: a jump detector on the decoded PCM found a discontinuity in the last
  4–5 frames of *every* packet, and only there. The RTP receive buffer was 1408 bytes including
  the 12-byte RTP header; PipeWire sends uncompressed ALAC (352 frames = 1424 bytes on the wire),
  so the tail of each packet was cut and the decoder read garbage. iPhones compress, their
  packets are shorter, hence "decent". Buffer 2048 bytes: zero jumps over a full run.
- Second finding: the RTP layer handed frames to the output ring only an eighth of the latency
  before they were due; with a sender that transmits close to real time the ring hovered at
  40 ms and dipped under a chunk. Hand-off is now half the latency ahead and a new stream
  waits for 100 ms in the ring before it starts: underruns 0.
- Third, the owner's requirement: when nothing plays, any device must be able to take the
  radio. One sender (PipeWire) kept its RTSP session open with silence and the receiver's
  single-connection loop left the iPhone waiting until it gave up ("Unable to connect"). A
  second client now takes over a session that has carried no sound for five seconds; verified
  with a silent stream from Fedora and a knock on port 5000.
- Also found: the earlier "Unable to connect" reports coincided with my log captures, which
  reset the board on opening the port. The port is now held open by one long-lived monitor.
- Headroom: 14 dB of trim put digital full scale exactly at the transmitter's 0 dBfs and
  pre-emphasised peaks into its limiter (OVERMOD in the log). Trim 18 dB; the resistor divider
  in the aux path will give this headroom back to the DAC.
- Released 0.2.0 with the first signed image.
- Owner asked what the missing divider costs. Answer with numbers in `audio-levels.md`: nothing
  audible over FM; the divider is for noise margin on the cable and goes in with the enclosure.
- Owner asked whether the frequency can be changed from a web page. It can, on the setup page
  (BOOT 5 s), with a restart; a LAN page without restart is now on the "possible later" list in
  `roadmap.md`, not scheduled. The owner will try the setup page himself.
- Owner tried the setup page from the phone: it opened, then threw him out, and the board stayed
  in setup mode. Log: the moment the phone joined, httpd logged "error in accept (23)", ENFILE,
  the socket table (default 10) was full: the AirPlay receiver's sockets, mDNS, the captive DNS,
  and iOS opening several connections at once. Fix: 24 sockets, and the AirPlay receiver is
  stopped for the duration of setup mode.
