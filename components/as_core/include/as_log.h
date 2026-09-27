#pragma once
// Event journal: one line per thing the device did (network, setup, OTA, button, transmitter,
// AirPlay sessions, errors), on the console and in a RAM ring that the status page can show.
// Never contains secrets. There is no card in this device, so nothing persists across a reboot
// except through the log of whoever is watching the serial port.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void as_log_init(void);
void as_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Copies up to `max_lines` most recent lines (newest last) into `out`, newline separated.
size_t as_log_tail(char *out, size_t cap, int max_lines);

#ifdef __cplusplus
}
#endif
