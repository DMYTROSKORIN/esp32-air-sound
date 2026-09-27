#pragma once
// The transmitter as the application sees it: bring it up from the stored configuration, keep it
// on air, put the current track into RDS. Wraps si4713.c.

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

// Scans the bus, brings the chip up, tunes and loads RDS from as_config. False when no chip answers.
bool fm_init(i2c_master_bus_handle_t bus, int rst_gpio);
bool fm_ok(void);
// Re-tune to the current configuration (after the portal or a remote changed it).
bool fm_apply_config(void);
// Radiotext: "artist - title" while something plays, the station's default text otherwise.
void fm_set_now_playing(const char *artist, const char *title);
void fm_clear_now_playing(void);
// One line for the journal: frequency, power, antenna cap, input level, RDS state.
void fm_status_line(char *out, size_t cap);
