#pragma once
// Wi-Fi station with automatic reconnect, plus the setup access point (open network
// "AirSound-Setup" with a captive page) used when no credentials are stored or when the
// owner holds BOOT. Descended from smartest-home's sh_net.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_NET_DOWN,
    AS_NET_CONNECTING,
    AS_NET_UP,
    AS_NET_PROVISIONING,
} as_net_state_t;

typedef void (*as_net_cb_t)(as_net_state_t state, void *user);

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} as_net_ap_t;

void as_net_init(as_net_cb_t cb, void *user);
// Connects with the stored credentials; opens the setup AP when none exist.
void as_net_start(void);
// Before as_net_start: MAX_MODEM power save with this listen interval (in beacons), for a device that
// can wait up to interval x 102 ms for traffic. Default MIN_MODEM, interval 3.
void as_net_set_power_save(bool max_modem, uint8_t listen_interval);
// Switches the radio off (no reconnects) and back on - a measurement of what the board draws without it.
void as_net_radio(bool on);
// Opens the setup AP now (keeps the station config; reboots after "Apply").
void as_net_start_provisioning(void);
as_net_state_t as_net_state(void);
// Scans for networks (blocking, a few seconds), strongest first, one row per SSID. Returns the count.
// Usable while the setup AP is up; it briefly disturbs clients connected to it.
int as_net_scan(as_net_ap_t *out, int max);
// Tries credentials with the setup AP still running, so the owner can be told what went wrong instead
// of watching a device reboot into silence. Leaves the station connected when it returns true; from the
// first call the automatic reconnect is off and the radio belongs to the setup portal until a reboot.
bool as_net_try_connect(const char *ssid, const char *pass, uint32_t timeout_ms);
// Why the last trial failed, in words meant for the setup page ("wrong password", "network not found").
const char *as_net_last_error(void);
int as_net_rssi(void);
const char *as_net_ip(void);
const char *as_net_ssid(void);
// mDNS host name derived from the station name: lower case, letters and digits, dashes between.
const char *as_net_hostname(void);
bool as_net_is_up(void);

#ifdef __cplusplus
}
#endif
