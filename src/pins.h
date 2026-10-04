#pragma once

// Heltec WiFi LoRa 32 V2 (ESP32 + SX1276 + SSD1306). 868 MHz module.
// Cross-checked with the Meshtastic heltec_v2 variant.

// Powers the OLED. Active LOW.
#define PIN_VEXT 21
#define VEXT_ON LOW

// SSD1306 128x64 OLED on I2C.
#define PIN_OLED_SDA 4
#define PIN_OLED_SCL 15
#define PIN_OLED_RST 16
#define OLED_ADDR 0x3C

// User button (PRG), active LOW.
#define PIN_BUTTON 0

#define PIN_LED 25

// Battery divider is 220k/100k (scale 3.2) and is connected while Vext is on.
// V2.0 senses on GPIO 13 (ADC2, only valid with WiFi off). V2.1 moved it to GPIO 37.
#define PIN_BATTERY_V2 13
#define PIN_BATTERY_V21 37

// SX1276 LoRa. DIO0 is RxDone / TxDone / CAD. No TCXO and no DIO2 RF switch.
#define PIN_LORA_SCK 5
#define PIN_LORA_MISO 19
#define PIN_LORA_MOSI 27
#define PIN_LORA_CS 18
#define PIN_LORA_RST 14
#define PIN_LORA_DIO0 26
#define PIN_LORA_DIO1 35
