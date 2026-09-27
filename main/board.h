#pragma once
// ESP32-S3-N16R8 devkit, left header row. Matches docs/wiring.md; change both together.

#define PIN_SI4713_RST   38
#define PIN_I2C_SCL      39
#define PIN_I2C_SDA      40

#define PIN_I2S_BCK      41
#define PIN_I2S_LRCK     42
#define PIN_I2S_DOUT      2

#define SI4713_ADDR_CS_HIGH 0x63   // CS open (module pull-up)
#define SI4713_ADDR_CS_LOW  0x11   // CS tied to GND

#define PIN_LED          48   // WS2812 on the devkit
#define PIN_BUTTON        0   // BOOT
