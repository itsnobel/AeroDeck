#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

static constexpr uint8_t NRF24_NUM_CHANNELS = 126; // 2400 MHz .. 2525 MHz
static constexpr size_t NRF24_WF_ROWS = 48;

class Nrf24Module {
public:
  enum SubView : uint8_t {
    VIEW_SPECTRUM_WF = 0,
    VIEW_PASSIVE_STATS,
    VIEW_COUNT
  };

  Nrf24Module();

  void enter(BusManager& bus, AerodeckGui& gui);
  void exit();
  bool isActive() const { return _active; }

  void handleInput(ButtonEvent ev);
  void tick();
  void render();

private:
  BusManager* _bus = nullptr;
  AerodeckGui* _gui = nullptr;
  bool _active = false;
  bool _radioReady = false;

  SubView _view = VIEW_SPECTRUM_WF;
  RF24 _radio;

  // Channel energy counts across 126 channels (2400 + ch MHz)
  uint16_t _chHits[NRF24_NUM_CHANNELS];
  uint16_t _maxChHits = 1;
  uint8_t _peakChannel = 0;

  // Compact waterfall history (rows x 126 columns -> rendered across 128px width)
  uint16_t _waterfall[NRF24_WF_ROWS][NRF24_NUM_CHANNELS];
  size_t _wfHead = 0;
  uint32_t _lastSweepMs = 0;

  // Passive RX frame counters (no TX / no injection)
  uint32_t _rxPacketsSeen = 0;
  uint32_t _crcOkPackets = 0;
  uint32_t _payloadBytesSeen = 0;
  uint8_t _monitorChannel = 2; // Typical 2.4 GHz channel offset
  bool _sniffEnabled = false;
  char _statusText[32];

  bool initRadioLocked();
  void powerDownRadioLocked();
  void sweepSpectrumLocked();
  void pollPassiveRxLocked();
  uint16_t hitLevelToColor(uint16_t hits, uint16_t maxHits) const;

  void drawHeaderStrip(const char* title, const char* rightHint);
  void drawSpectrumView();
  void drawPassiveStatsView();
};
