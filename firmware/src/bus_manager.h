#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config_pins.h"

enum ButtonEvent : uint8_t {
  BTN_EVT_NONE = 0,
  BTN_EVT_SELECT,
  BTN_EVT_LEFT,
  BTN_EVT_UP,
  BTN_EVT_RIGHT,
  BTN_EVT_DOWN
};

class BusManager {
public:
  BusManager();
  ~BusManager();

  bool begin();

  // SPI2 mutual exclusion across TFT, SD, CC1101, and NRF24
  bool lockSpi(TickType_t timeoutTicks = portMAX_DELAY);
  bool tryLockSpi(uint32_t timeoutMs);
  void unlockSpi();

  // Assert all CS pins HIGH, then drive only activeCsPin LOW (or -1 for all HIGH)
  void releaseAllCs();
  void selectOnlyCs(int activeCsPin);

  // Analog inputs (ADC1)
  uint16_t readButtonMilliVolts();
  void updateInputs();
  ButtonEvent consumeButtonEvent();
  const char* buttonName(ButtonEvent ev) const;

  float readBatteryVolts();
  uint8_t readBatteryPercent();

private:
  SemaphoreHandle_t _spiMutex = nullptr;
  bool _spiStarted = false;

  ButtonEvent _debouncedBtn = BTN_EVT_NONE;
  ButtonEvent _lastRawBtn = BTN_EVT_NONE;
  uint32_t _lastChangeMs = 0;
  ButtonEvent _pendingEvent = BTN_EVT_NONE;

  void remapUsbJtagPins();
  void initAllCsHigh();
  ButtonEvent classifyButtonVoltage(uint16_t mv) const;
};

class SpiGuard {
public:
  explicit SpiGuard(BusManager& bus, TickType_t timeoutTicks = portMAX_DELAY)
      : _bus(bus), _locked(bus.lockSpi(timeoutTicks)) {}

  ~SpiGuard() {
    if (_locked) {
      _bus.releaseAllCs();
      _bus.unlockSpi();
    }
  }

  SpiGuard(const SpiGuard&) = delete;
  SpiGuard& operator=(const SpiGuard&) = delete;

  bool acquired() const { return _locked; }

private:
  BusManager& _bus;
  bool _locked;
};
