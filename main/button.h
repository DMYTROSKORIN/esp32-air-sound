#pragma once
// The BOOT button (GPIO 0), bastion's two-tier hold: release between 5 and 10 s reopens the
// setup portal, holding to 10 s is a factory reset. A short press is reported separately so the
// application can use it for play/pause once there is a stream to control. A level that is
// already low when the firmware starts is ignored: the hold timers only start from a press that
// begins after boot, which keeps the USB auto-reset circuit and the download mode out of it.

#include <stdint.h>

typedef enum { BUTTON_SHORT, BUTTON_SETUP, BUTTON_RESET } button_event_t;
typedef void (*button_cb_t)(button_event_t ev);

void button_init(int gpio, button_cb_t cb);
