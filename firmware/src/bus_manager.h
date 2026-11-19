// ============================================================================
// Aerodeck V2 - Shared SPI2 Bus Manager
// ============================================================================
// Owns:
//   - SPI2 bus initialization (SCLK=6, MOSI=7, MISO=2)
//   - A FreeRTOS mutex guarding ALL transactions across TFT/SD/CC1101/NRF24
//   - CS pin state (all HIGH at idle)
//   - GPIO13/GPIO14 reclamation from native USB Serial/JTAG
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace BusManager {

// Initialize the SPI2 bus, the SPI mutex, and all CS pins.
// Call exactly once before any SPI device is touched.
// Returns false only if the mutex cannot be allocated.
bool begin();

// Acquire / release the SPI bus mutex. ALWAYS check lock()'s return value.
bool lock(uint32_t timeoutMs = 100);
void unlock();

// Underlying SPI object (pass to LovyanGFX, RadioLib, RF24, etc.)
SPIClass& spi();

// CS helpers. Caller MUST hold the lock while using these.
void csAllHigh();
void csLow(uint8_t pin);
void csHigh(uint8_t pin);

// SPI transaction helpers (pair beginTransaction/endTransaction while locked).
void beginTransaction(const SPISettings& settings);
void endTransaction();

// Status
bool isReady();

// Dump pin states + heap to Serial (for sanity checks).
void printDiagnostics();

} // namespace BusManager
