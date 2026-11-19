#pragma once

#include <Arduino.h>

// ============================================================================
// STRICTLY RESERVED PINS (DO NOT ASSIGN TO PERIPHERALS)
// GPIO11 = U0TXD, GPIO12 = U0RXD, GPIO15 = Flash/PSRAM SPICS1, GPIO28 = BOOT
// ============================================================================

// Shared SPI2 Bus (Note: GPIO2 and GPIO7 are strapping pins -> keep CS HIGH at boot)
static constexpr int PIN_SPI_SCLK = 6;
static constexpr int PIN_SPI_MOSI = 7;
static constexpr int PIN_SPI_MISO = 2;

// SPI2 Chip Select & Control Pins (each CS gets a 10k ohm pull-up to 3.3V)
static constexpr int PIN_TFT_CS      = 10;
static constexpr int PIN_TFT_DC      = 8;
static constexpr int PIN_TFT_RST     = 23;

static constexpr int PIN_SD_CS       = 14; // Reconfigured away from USB Serial/JTAG

static constexpr int PIN_CC1101_CSN  = 24;
static constexpr int PIN_CC1101_GDO0 = 9;

static constexpr int PIN_NRF24_CSN   = 26;
static constexpr int PIN_NRF24_CE    = 25;

// UART1 for ATK-NEO-6M GPS (verify no 32.768 kHz XTAL occupies GPIO0/GPIO1 on board)
static constexpr int PIN_GPS_RX_FROM_TX = 0; // ESP32-C5 RX <- GPS TX
static constexpr int PIN_GPS_TX_TO_RX   = 1; // ESP32-C5 TX -> GPS RX

// Infrared Lab (VS1838B RX + 2N2222A / 940nm IR LED TX)
static constexpr int PIN_IR_RX = 3;
static constexpr int PIN_IR_TX = 13; // Reconfigured away from USB Serial/JTAG

// Analog Inputs (ADC1)
static constexpr int PIN_BTN_ADC = 5; // 5-button resistor ladder + 100nF filter cap
static constexpr int PIN_BAT_ADC = 4; // 100k/100k voltage divider + 100nF filter cap

// Status LED
static constexpr int PIN_NEOPIXEL = 27;

// Battery voltage thresholds (cell voltage after 2.0x divider scaling)
static constexpr float BAT_VOLT_MIN = 3.20f;
static constexpr float BAT_VOLT_MAX = 4.20f;
