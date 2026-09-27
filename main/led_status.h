#pragma once
// The one RGB LED on the devkit (WS2812 on GPIO 48) tells the whole story without a serial port.
// The colour language is esp32-s3-n16r8-bastion's, with the radio's states in place of the VPN's:
//
//   solid yellow          setup mode: join AirSound-Setup and open 192.168.4.1
//   amber blink           BOOT held; faster once the hold passes 5 s (10 s = factory reset)
//   fast red 1.5 s        factory reset accepted, rebooting
//   smooth blue breathing connecting to Wi-Fi
//   two green flashes     on the network, transmitter on air, nobody streaming
//   two green + one violet  a phone/laptop is streaming to us
//   fast red blinking     transmitter not answering (I2C), or no Wi-Fi for too long
//
// Flash edges are softened over ~20 ms; alerts (red, amber) stay hard-edged on purpose.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_SETUP,
    LED_CONNECTING,
    LED_ONLINE,
    LED_STREAMING,
    LED_FAULT,
} led_state_t;

void led_status_init(int gpio);
void led_status_set(led_state_t st);
// BOOT hold feedback: 0 = not held; otherwise the hold length in ms (drives the amber blink).
void led_status_hold(uint32_t held_ms);
// Blocks ~1.5 s flashing red; used right before a factory reset.
void led_status_reset_flash(void);
