#pragma once
// The AirPlay receiver as the application sees it. Advertises the station name over Bonjour once
// the network is up, takes the stream from a phone or a laptop, hands decoded PCM to audio_out
// and the track's metadata to the transmitter's RDS.

#include <stdbool.h>

typedef void (*airplay_state_cb_t)(bool streaming);

void airplay_init(airplay_state_cb_t cb);
// Call once the station interface has an address. Safe to call again after a reconnect.
void airplay_start(void);
bool airplay_streaming(void);
// Transport commands back to the sender (DACP): play/pause toggle, next, previous.
void airplay_toggle(void);
void airplay_next(void);
void airplay_prev(void);
