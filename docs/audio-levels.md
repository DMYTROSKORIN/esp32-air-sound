# Audio levels: the DAC, the transmitter input, and the resistor divider

Written 2026-09-28 after the owner asked how much sound quality is lost without the divider.
Short answer: nothing you can hear over FM. The longer answer is below, so the decision to
postpone the divider is on record together with the numbers behind it.

## The mismatch

| Point in the chain | Level |
|--------------------|-------|
| PCM5102 line output at digital full scale | 2.1 Vrms (about 3.0 Vpk) |
| Si4713 line input, most tolerant setting (`TX_LINE_INPUT_LEVEL` attenuation 3) | 636 mVpk full scale, 60 kΩ |

The DAC is about 4.7 times (13.5 dB) too hot for the transmitter. Something has to take that out.

## What the firmware does today

A digital trim, `CONFIG_AIRSOUND_LINE_TRIM_DB`, 18 dB by default: 13.5 dB to match the levels and
about 4 dB of headroom so that pre-emphasised peaks stay out of the transmitter's limiter (with 14 dB
the log showed `OVERMOD` on bright material). The trim is applied in 32-bit arithmetic on the way to
32-bit I2S slots, so the 16-bit source keeps every bit; what moves is the level relative to the
DAC's own noise floor.

## What is lost, in numbers

| Link | Dynamic range |
|------|---------------|
| PCM5102 at full scale | ~112 dB |
| PCM5102 with 18 dB of digital trim (today) | ~94 dB |
| Si4713 input and modulator | roughly 70–80 dB |
| Stereo FM at a receiver, good conditions | 55–65 dB |

The FM link is the bottleneck, the transmitter after it. The DAC's remaining 94 dB sits two
orders of magnitude above what the air carries. Audibly: no difference.

## Where a divider does help

With the divider the signal on the aux cable is five times larger, so any pickup on that cable
(Wi-Fi, USB, a switching supply) ends up 14 dB further below the music. On a breadboard with
wires everywhere that can be the difference between silence and a faint hum in the pauses.
It also gives the transmitter's limiter a cleaner margin.

## The divider, when it is built

Per channel, between the DAC output and the transmitter input:

```
DAC L (or R) ──[ 10 kΩ ]──┬── Si4713 LIN (or RIN)
                          │
                       [ 2.7 kΩ ]
                          │
                         GND
```

10 kΩ series and 2.7 kΩ to ground, with the Si4713's 60 kΩ input in parallel with the 2.7 kΩ,
divide by about 4.9 (13.8 dB): 2.1 Vrms becomes 0.61 Vpk, just under the input's full scale.
10 kΩ / 3.3 kΩ also works and leaves 1–2 dB of digital trim in place. Any quarter-watt 5 %
resistors. On the bench the aux cable can go: the DAC has free `L`, `G`, `R` holes next to its
jack, the Si4713 has `LIN` and `RIN` on its header, and four resistors on the breadboard join them.

Firmware change when it exists: `CONFIG_AIRSOUND_LINE_TRIM_DB` from 18 to 0 (or 2–3 with 3.3 kΩ).

## Decision

Postponed until the move into an enclosure, together with proper wiring. Not urgent, not audible;
worth doing then for the noise margin. Recorded in `decisions.md`.
