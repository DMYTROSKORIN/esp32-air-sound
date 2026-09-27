# MQTT contract (step 3, draft for review)

How the smartest-home remote will drive the radio. This file is the whole of what this
repository says about the hub: the topics, the payloads, and nothing about the hub's insides.
Written the night of 2026-09-28 from the hub's existing topic layout, for the owner to correct
in the morning; no firmware implements it yet.

## Topics

Same layout every smartest-home board uses, node id `air-sound` (the station name, lower-cased):

```
smartest-home/v1/air-sound/status            retained, LWT "offline"
smartest-home/v1/air-sound/telemetry         every 60 s and on change
smartest-home/v1/air-sound/event             session start/stop, track change, setup, OTA
smartest-home/v1/air-sound/command           hub/remote -> radio
smartest-home/v1/air-sound/command/result    radio -> hub, one per command
```

Envelope as elsewhere: `{"v":1,"ts":<unix>,"node":"air-sound", ...}`. Commands carry
`{"type":"radio.set","request_id":"<id>","args":{...}}`; results answer with the same
`request_id`, `"ok":true|false` and `"message"`.

## Status (retained)

```json
{"v":1,"ts":0,"node":"air-sound","state":"online",
 "fw":"0.2.0","name":"Air-Sound",
 "radio":{"freq_mhz":76.50,"power_dbuv":105,"rds_ps":"AirSound","antcap":102,"on_air":true},
 "stream":{"active":false,"sender":"","artist":"","title":"","volume_db":0},
 "net":{"rssi":-61,"ip":"<board-ip>"}}
```

## Telemetry

```json
{"v":1,"ts":0,"node":"air-sound",
 "input_dbfs":-18,"overmod":false,"queued_ms":98,"underruns":0,"jumps":0,
 "rssi":-61,"heap":6330000,"uptime_s":12345}
```

`input_dbfs`, `overmod` are the transmitter's own audio meter; `queued_ms`, `underruns`, `jumps`
are the stream health counters that found the PipeWire packet bug. They stay in telemetry so a
bad night on the radio can be read off a chart instead of a serial port.

## Commands

| type | args | effect |
|------|------|--------|
| `radio.set` | `freq_mhz` (76.00–108.00, step 0.05), `power_dbuv` (88–115), `rds_ps` (≤8), `name` (≤32) | Applied at once, stored in NVS; retune is glitch-free for the listener apart from the frequency change itself. A `name` change restarts the AirPlay advertisement (senders see the new name within seconds). |
| `audio.set` | `volume` 0–100 | Device-side volume on top of the sender's; stored. |
| `playback` | `action`: `toggle`, `play`, `pause`, `next`, `prev` | Sent to the current sender over DACP; `ok:false` when nobody is streaming. |
| `stream.kick` | — | Ends the current session so another device can take the radio now, without waiting five seconds of silence. |
| `system.setup` | — | Opens the setup access point (what a 5 s BOOT hold does). |
| `system.restart` | — | |
| `system.ota` | `url` (optional) | Installs the given signed image, or checks GitHub and installs the newest release. |

The remote's tiles, in the order the owner uses the radio: frequency up/down by 0.05 MHz,
play/pause, next, volume, and a status line with the track title. Presets (a few named
frequencies) can be a later addition to `radio.set`.

## Events

`{"type":"stream.start","sender":"iPhone"}`, `{"type":"stream.stop"}`,
`{"type":"track","artist":"…","title":"…"}`, `{"type":"radio.retuned","freq_mhz":…}`,
`{"type":"setup.opened"}`, `{"type":"ota","version":"…","result":"…"}`.

## Broker credentials: two ways, owner to choose

1. **Pairing code through the hub**, exactly as the remote and the dashboard do: the setup page
   gains "hub address" and "pairing code" fields, the device enrols and receives broker, user,
   password and its node id. Consistent with the rest of the house; the MQTT password never
   passes through a phone keyboard. Needs the hub to know the kind `air-sound`.
2. **Manual broker fields** on the setup page (host, port, user, password). Simpler on the
   device, one more form for the owner, and a password typed on a phone.

Recommendation: 1. The provisioning code for it exists in smartest-home's `sh_provision`
(the enrol call was removed from this repository's copy and can come back behind a
"Smart home hub (optional)" section of the page). Without a hub the radio stays a plain
AirPlay receiver; MQTT is an addition, never a requirement.

## What stays out of this repository

Hub addresses, broker addresses, credentials, the hub's pairing implementation, the remote's
UI. The remote is "an MQTT client that publishes to `.../command`"; the rest is smartest-home's.
