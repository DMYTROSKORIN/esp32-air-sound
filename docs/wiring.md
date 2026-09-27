# Wiring the bench

The first assembly, for someone doing it for the first time. Powered from the ESP32's USB
port; the buck converter is not part of this step. The microphone is not part of it either
(it is step 4 in `roadmap.md`). Nothing here needs a soldering iron except one wire: the
antenna.

## What you need

- ESP32-S3-N16R8 devkit, already on the two breadboards.
- GY-PCM5102 DAC and CJMCU-4713 (Si4713) with their headers, both plug into a breadboard.
- Male-to-male Dupont jumper wires: 14 for the signals and power, a few spare.
- One 3.5 mm stereo male-to-male audio cable (aux cable). This is the DAC → transmitter link.
- About 75 cm of insulated solid or stranded wire for the antenna, soldered into the `Ant` hole.
- A multimeter. Two checks below are not optional.
- USB-C data cable to the computer.

Colour convention used below, so the bench and the photos agree:
red 5 V, black GND, orange 3V3, yellow SCL, green SDA, white RST, blue/purple/grey I2S.

## The picture

```
                 USB-C (UART side)
                       │
        ┌──────────────┴───────────────┐
        │        ESP32-S3-N16R8        │
        │                              │
  5Vin ─┤ 5Vin                   GPIO12├─── BCK ──┐
   GND ─┤ GND                    GPIO11├─── LCK ──┤   ┌──────────────┐
        │                        GPIO10├─── DIN ──┼──▶│  GY-PCM5102  │ VIN ◀── 5 V
        │                              │          │   │  I2S DAC     │ GND ◀── GND
        │                         GPIO9├─── SCL ──┼─┐ │              │ SCK ◀── GND (!)
        │                         GPIO8├─── SDA ──┼─┼▶│  3.5 mm jack │──┐
        │                         GPIO4├─── RST ──┼─┼┐└──────────────┘  │ aux cable
        │                           3V3├──────────┼─┼┼───────────────────┼─▶ 3V3 rail (unused in MVP)
        └──────────────────────────────┘          │ ││ ┌──────────────┐  │
                                                  │ │└▶│ RST          │  │
                                                  │ └─▶│ SCL  CJMCU-  │  │
                                                  └───▶│ SDA  4713    │  │
                                                       │ (Si4713)     │  │
                                             5 V ─────▶│ VIN          │  │
                                             GND ─────▶│ GND          │  │
                                                       │ CS   (open)  │  │
                                                       │ 3.5 mm jack  │◀─┘
                                                       │ Ant ─────────┼──── ~75 cm wire
                                                       └──────────────┘
```

## Every connection

Power first. The devkit's `5Vin` pin carries the USB 5 V out to the header when the board is
powered over USB, so it feeds the two modules.

| # | From (ESP32 devkit pin) | To | Wire | Notes |
|---|--------------------------|----|------|-------|
| 1 | `5Vin` (top right) | right breadboard `+` rail | red | this rail is now 5 V |
| 2 | `GND` (top right, next to 5Vin) | right breadboard `−` rail | black | this rail is now GND |
| 3 | `+` rail | PCM5102 `VIN` | red | |
| 4 | `−` rail | PCM5102 `GND` | black | |
| 5 | `−` rail | PCM5102 `SCK` | black | **required**: master clock unused, PLL runs from BCK. Without it the DAC is silent. |
| 6 | `GPIO12` | PCM5102 `BCK` | blue | I2S bit clock |
| 7 | `GPIO11` | PCM5102 `LCK` | purple | I2S word select (LRCK) |
| 8 | `GPIO10` | PCM5102 `DIN` | grey | I2S data |
| 9 | `+` rail | Si4713 `VIN` | red | the module regulates to 3.0 V on board |
| 10 | `−` rail | Si4713 `GND` | black | |
| 11 | `GPIO9` | Si4713 `SCL` | yellow | I2C clock; module has 10 kΩ pull-ups |
| 12 | `GPIO8` | Si4713 `SDA` | green | I2C data |
| 13 | `GPIO4` | Si4713 `RST` | white | firmware pulses it low at start; chip is dead until then |
| 14 | `3V3` (bottom right) | second `+` rail on the right breadboard | orange | not needed in the MVP, ready for the microphone; keep it on its own rail, never on the 5 V rail |
| — | PCM5102 jack | Si4713 jack | aux cable | line-level audio, stereo |
| — | Si4713 `Ant` hole | ~75 cm wire | solder | quarter wave near 100 MHz; hang it vertically |

Left unconnected on purpose: Si4713 `CS` (open = I2C address `0x63`; tie to GND only if you
want `0x11`), `GP1`, `GP2`, `3Vo`, `LIN`, `RIN` (the jack is in parallel with them); PCM5102
`L/G/R/G` pads (the jack is in parallel with them).

## Where to put the modules on the breadboard

The devkit occupies column `a` on the right breadboard and column `e` on the left one, so
columns `b–e` on the right are free next to every devkit pin, and the `f–j` half of the right
breadboard is completely free. Put both modules there, headers in column `j`, one pin per
row, so each pin has holes `f–i` for wires:

- Si4713, 11 pins: rows **30–40**, column `j`. Row 30 = `RST` … row 40 = `RIN`. The jack
  points outward, the antenna wire has room.
- PCM5102, 6 pins: rows **45–50**, column `j`. Row 45 = `SCK` … row 50 = `VIN`. The jack
  points outward, toward the Si4713 jack, so the aux cable is short.

A single-row header always goes *along* the row numbers (each pin in its own numbered row),
never along one row: the five holes of one row are shorted together.

Rails: on most 830-point breadboards the `+`/`−` rails are broken in the middle, around row
30. Check with the meter (continuity between row 5 and row 55 of the same rail); if broken,
bridge each rail with a short jumper.

## Order of work

1. **USB unplugged.** Nothing is powered while wires move.
2. Plug the two modules in (rows above). Press them in straight; the headers are stiff.
3. Wires 1–2 (rails), then 3–5 (DAC power and SCK), then 9–10 (Si4713 power).
4. Meter, continuity mode: `+` rail to `−` rail must **not** beep. If it beeps, find the short
   before going on; a module in the wrong rows is the usual cause.
5. Signal wires 6–8 and 11–13. Wire 14 last.
6. Turn the DAC over and confirm the four jumpers with the meter (continuity from the middle
   pad to the `H` or `L` pad): expected `H1L=L`, `H2L=L`, `H3L=H`, `H4L=L`. `H3L` on `L` means
   the DAC is muted in hardware and no firmware will fix it; it takes a solder blob to move it.
7. Solder the antenna wire into `Ant`. It is the only solder joint. Keep it away from the
   USB cable and the Wi-Fi antenna.
8. Aux cable between the two jacks.
9. USB in. The devkit's LED comes on. Meter, voltage mode: `+` rail ≈ 4.7–5.1 V, `3V3` pin
   ≈ 3.3 V, PCM5102 `A3V3` pad (back side) ≈ 3.3 V, Si4713 `3Vo` pin ≈ 3.0 V. Touch each
   module after a minute: warm is fine, hot is not.

## What "working" looks like at the end of step 1 (firmware, see roadmap)

- The firmware's I2C scan prints one device at `0x63`.
- The Si4713 reports tune status at the chosen frequency and a non-zero antenna capacitor.
- A 1 kHz test tone from the DAC is heard on an FM radio tuned to that frequency, in stereo,
  with the RDS name on the radio's display within ~10 s.

## Things that will bite

- **DAC silent**: `SCK` not on GND, or `H3L` on `L`, or BCK/LCK swapped. In that order.
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
