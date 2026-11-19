// ============================================================================
// Aerodeck V2 - Central Pin & Hardware Configuration
// ============================================================================
// Target: ESP32-C5 (RISC-V single-core @ 240 MHz, 384 KB HP SRAM)
//         Dual-band 2.4/5 GHz Wi-Fi 6 + BLE 5
// ============================================================================
#pragma once
#include <Arduino.h>

// ----- Shared SPI2 Bus ------------------------------------------------------
// NOTE: GPIO2 (MISO) and GPIO7 (MOSI) are strapping pins. Every CS line MUST
//       be driven HIGH before SPI.begin() so no slave drives MISO at boot.
#define PIN_SPI_SCLK        6
#define PIN_SPI_MOSI        7
#define PIN_SPI_MISO        2

// ----- TFT Display (1.8" ST7735, 128x160) -----------------------------------
#define PIN_TFT_CS          10   // 10k pull-up
#define PIN_TFT_DC          8
#define PIN_TFT_RST         23
// Backlight tied to hardware 3.3 V rail (always on)

// ----- MicroSD Card (on TFT module) -----------------------------------------
#define PIN_SD_CS           14   // 10k pull-up; shares pad with USB D+ (reconfig)

// ----- CC1101 Sub-1 GHz Radio -----------------------------------------------
#define PIN_CC_CSN          24   // 10k pull-up
#define PIN_CC_GDO0         9
// CC_GDO2 = NC

// ----- NRF24L01+ PA+LNA Radio -----------------------------------------------
#define PIN_NRF_CSN         26   // 10k pull-up
#define PIN_NRF_CE          25
// NRF_IRQ = NC

// ----- ATK-NEO-6M GPS (UART1) -----------------------------------------------
#define PIN_GPS_RX          0    // ESP32-C5 RX  <- GPS TX
#define PIN_GPS_TX          1    // ESP32-C5 TX  -> GPS RX (UBX config)
#define GPS_UART_NUM        1

// ----- IR Subsystem ---------------------------------------------------------
#define PIN_IR_RX           3    // VS1838B, external 10k-22k pull-up
#define PIN_IR_TX           13   // 940 nm LED via 2N2222A; shares pad with USB D-

// ----- Analog Inputs --------------------------------------------------------
#define PIN_BTN_ADC         5    // ADC1_CH4 - 5-button series ladder
#define PIN_BAT_ADC         4    // ADC1_CH3 - 100k/100k divider + 100nF

// ----- Status LED -----------------------------------------------------------
#define PIN_NEOPIXEL        27

// ============================================================================
// RESERVED / DO NOT ASSIGN
// ============================================================================
// GPIO11  U0TXD (UART0 console TX)
// GPIO12  U0RXD (UART0 console RX)
// GPIO15  Flash/PSRAM SPICS1
// GPIO28  BOOT strapping
// ============================================================================

// ============================================================================
// Button Ladder Voltage Bands (mV)
// ============================================================================
// Nominal: SELECT=0.000 V, LEFT=0.595 V, UP=1.171 V,
//          RIGHT=1.666 V, DOWN=2.207 V, IDLE=3.300 V
// Thresholds are midpoints between adjacent nominals.
#define BTN_TH_SELECT_LEFT      300    // <  300 -> SELECT
#define BTN_TH_LEFT_UP          880    // 300..880 -> LEFT
#define BTN_TH_UP_RIGHT         1420   // 880..1420 -> UP
#define BTN_TH_RIGHT_DOWN       1940   // 1420..1940 -> RIGHT
#define BTN_TH_DOWN_IDLE        2750   // 1940..2750 -> DOWN, >2750 -> IDLE

// ============================================================================
// Battery Divider (1:1, so cell voltage = 2 x ADC voltage)
// ============================================================================
#define BAT_DIVIDER_RATIO       2.0f
#define BAT_MV_MIN              3000   // 3.0 V (empty)
#define BAT_MV_MAX              4200   // 4.2 V (full)

// ============================================================================
// SPI Clock Constraints (per-device)
// ============================================================================
#define SPI_CLOCK_TFT_HZ        20000000UL   // 20 MHz - ST7735 over dupont
#define SPI_CLOCK_SD_HZ         25000000UL   // 25 MHz - SD card (conservative)
#define SPI_CLOCK_CC1101_HZ      8000000UL   //  8 MHz - CC1101 max
#define SPI_CLOCK_NRF24_HZ      10000000UL   // 10 MHz - NRF24 max
