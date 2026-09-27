#pragma once
// The setup access point and its captive page. Started by as_net when the device has no Wi-Fi
// credentials, or when the owner holds BOOT for five seconds. One page, designed for a phone:
// the network (picked from a scan or typed), its password, the station name, the FM frequency,
// the transmit power and whether releases may install themselves. The device proves the
// credentials work while the setup network is still up, and only then writes NVS and reboots.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AS_SETUP_SSID "AirSound-Setup"

// Called whenever the setup state changes (for the LED / journal). Runs on the portal's worker task.
typedef void (*as_provision_status_cb_t)(const char *message, bool done, bool failed);

void as_provision_start(void);
void as_provision_set_status_cb(as_provision_status_cb_t cb);

#ifdef __cplusplus
}
#endif
