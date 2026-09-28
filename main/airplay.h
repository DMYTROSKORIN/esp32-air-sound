#pragma once
// The AirPlay receiver as the application sees it. Advertises the station name over Bonjour once
// the network is up, takes the stream from a phone or a laptop, hands decoded PCM to audio_out
// and the track's metadata to the transmitter's RDS.

#include <stdbool.h>
#include <stdint.h>

typedef void (*airplay_state_cb_t)(bool streaming);

void airplay_init(airplay_state_cb_t cb);
// Call once the station interface has an address. Safe to call again after a reconnect.
void airplay_start(void);
// Withdraws the Bonjour advertisement and closes the receiver (setup mode: its sockets are needed
// by the portal and nobody should be streaming while the owner reconfigures the radio).
void airplay_stop(void);
bool airplay_streaming(void);
// Diagnostics: count of implausible sample-to-sample jumps seen in the decoded stream.
uint32_t airplay_jumps(void);
// Transport commands back to the sender (DACP): play/pause toggle, next, previous.
void airplay_toggle(void);
void airplay_next(void);
void airplay_prev(void);
