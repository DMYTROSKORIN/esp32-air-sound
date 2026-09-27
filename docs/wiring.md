# Wiring the bench

The first assembly, for someone doing it for the first time. Powered from the ESP32's USB
port; the buck converter is not part of this step. The microphone is not part of it either
(it is step 4 in `roadmap.md`). Nothing here needs a soldering iron except one wire: the
antenna.

## What you need

- ESP32-S3-N16R8 devkit, already on the two breadboards.
- GY-PCM5102 DAC and CJMCU-4713 (Si4713), already plugged into the left breadboard.
- Male-to-male Dupont jumper wires: 13 for the signals and power, a few spare.
- One 3.5 mm stereo male-to-male audio cable (aux cable). This is the DAC → transmitter link.
- About 75 cm of insulated solid or stranded wire for the antenna, soldered into the `Ant` hole.
- A multimeter. Two checks below are not optional.
- USB-C data cable to the computer.

Colour convention used below, so the bench and the photos agree:
red 5 V, black GND, orange 3V3, yellow SCL, green SDA, white RST, blue/purple/grey I2S.

## Where the modules are

Both modules were placed on the **left** breadboard, headers in column `j`, bodies hanging
over the gap between the two boards (photo in the journal entry). The devkit's left header row
is also in column `j` of that board, rows 1–22. That makes the left header the natural source
of the signals: every wire stays on one board, and only the +5 V wire crosses the devkit.

| Part | Column | Rows | Pin order, top to bottom |
|------|--------|------|--------------------------|
| Devkit, left row | `j` | 1–22 | GND GND 19 20 21 47 48 45 0 35 36 37 **38 39 40 41 42 2** 1 RX TX **GND** |
| Si4713 | `j` | 30–40 | RST CS SCL SDA GP1 GP2 3Vo GND VIN LIN RIN |
| PCM5102 | `j` | 58–63 | SCK BCK DIN LCK GND VIN (VIN in the last row) |

Rule for the wires: **signals in column `i`, power in column `f`**. Any hole `f–i` of a row is
connected to the header pin in `j` of that row. Row numbers read from the photo can be off by
one; the pin names on the boards are the truth, the table below then shifts with them.

Rails: on this board the `+`/`−` pair is on the left edge, `+` outermost. Most 830-point boards
break the rails in the middle; check continuity between row 5 and row 60 on one rail, bridge
with a jumper if it is broken.

## Every connection

| # | From | To | Wire | Notes |
|---|------|----|------|-------|
| 1 | right board `b1` (row of devkit `5Vin`, top right pin) | left board `+` rail | red, long | the only wire that crosses the devkit; route it over the top edge past the USB ports |
| 2 | `f22` (bottom `GND` of the left header) | left board `−` rail | black | |
| 3 | `+` rail | `f37` (Si4713 `VIN`) | red | module regulates to 3.0 V |
| 4 | `−` rail | `f36` (Si4713 `GND`) | black | |
| 5 | `+` rail | `f63` (PCM5102 `VIN`) | red | |
| 6 | `−` rail | `f62` (PCM5102 `GND`) | black | |
| 7 | `−` rail | `f58` (PCM5102 `SCK`) | black | **required**: no MCLK, PLL from BCK; without it the DAC is silent |
| 8 | `i13` (GPIO38) | `i30` (Si4713 `RST`) | white | firmware pulses it at start |
| 9 | `i14` (GPIO39) | `i31` (Si4713 `SCL`) | yellow | pull-ups on the module |
| 10 | `i15` (GPIO40) | `i32` (Si4713 `SDA`) | green | |
| 11 | `i16` (GPIO41) | `i59` (PCM5102 `BCK`) | blue | |
| 12 | `i17` (GPIO42) | `i61` (PCM5102 `LCK`) | purple | |
| 13 | `i18` (GPIO2) | `i60` (PCM5102 `DIN`) | grey | |
| 14 | PCM5102 jack | Si4713 jack | 3.5 mm aux | already plugged in on the bench |
| 15 | Si4713 `Ant` hole | ~75 cm wire | solder | top-right corner of the module, over the gap |

Left unconnected on purpose: Si4713 `CS` (open = `0x63`), `GP1`, `GP2`, `3Vo`, `LIN`, `RIN`;
PCM5102 `R/G/L` pads and holes `1–4`; the devkit's whole right row except `5Vin`, and `3V3`.

Firmware pins that must match: SDA 40, SCL 39, RST 38, BCK 41, LRCK 42, DOUT 2, no MCLK.

## Order of work

1. **USB unplugged.** Move the devkit's Wi-Fi whip onto the right breadboard, which is empty.
2. Pull the DAC, check the four back-side jumpers with the meter (`H1L=L`, `H2L=L`, `H3L=H`,
   `H4L=L`; `H3L` on `L` is a hardware mute), put it back in column `j`, rows 58–63.
3. Confirm the rows: Si4713 `RST` in 30 and `RIN` in 40, PCM5102 `SCK` in 58 and `VIN` in 63.
4. Wires 1–2, then 3–4, then 5–7.
5. Meter, continuity: `+` rail to `−` rail must not beep. Rows 36/37 and 62/63 are the easy
   places to be one row off.
6. Signal wires 8–13, all column `i` to column `i`.
7. Solder the antenna wire into `Ant` (pull the module out to do it). Hang the wire vertically,
   away from the USB cable and the Wi-Fi whip.
8. Check both aux plugs are seated.
9. USB into the UART port. LED on.
10. Meter, DC volts, black probe on the `−` rail: `+` rail 4.7–5.1 V, Si4713 `3Vo` (`g35`) ≈ 3.0 V,
    devkit `3V3` ≈ 3.3 V.
11. After a minute, touch the modules: warm is fine, hot is not.

## What "working" looks like at the end of step 1 (firmware, see roadmap)

- The firmware's I2C scan prints one device at `0x63`.
- The Si4713 reports tune status at the chosen frequency and a non-zero antenna capacitor.
- A 1 kHz test tone from the DAC is heard on an FM radio tuned to that frequency, in stereo,
  with the RDS name on the radio's display within ~10 s.

## Things that will bite

- **DAC silent**: `SCK` (`f58`) not on GND, or `H3L` on `L`, or BCK/LCK swapped (`i59`/`i61`). In that order.
- **I2C scan finds nothing**: `RST` not pulsed (chip stays in reset), SDA/SCL swapped, or the
  module is at `0x11` because `CS` is pulled low on this clone; scan both addresses.
- **Loud, distorted, or the transmitter's limiter is always on**: the PCM5102 puts out 2.1 Vrms
  (≈3 Vpk), the Si4713 line input tops out at 636 mVpk on its most tolerant setting. The MVP
  fixes this in firmware: line-input level setting 3 (636 mV) on the Si4713 and the DAC's
  digital volume around −14 dB. A resistor divider in the aux path is the hardware fix later.
- **Hum or hiss**: the aux cable and the antenna wire running side by side; ground loop through
  the computer. Move the wires first, blame the boards last.
- **Wi-Fi drops when transmitting**: the FM wire near the 2.4 GHz whip. Separate them.
- Powering the devkit from `5Vin` and USB at the same time is for later, with the buck, and
  only after checking how this devkit's 5 V pin is protected. Not in this step.
