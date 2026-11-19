#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RadioLib.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

static constexpr size_t SUBGHZ_MAX_PULSES = 256;
static constexpr size_t SUBGHZ_WF_COLS = 128;
static constexpr size_t SUBGHZ_WF_ROWS = 48;

enum SubGhzBand : uint8_t {
  BAND_315_MHZ = 0,
  BAND_433_MHZ,
  BAND_868_MHZ,
  BAND_915_MHZ,
  BAND_COUNT
};

class SubGhzModule {
public:
  enum SubView : uint8_t {
    VIEW_WATERFALL = 0,
    VIEW_CAPTURE_REPLAY,
    VIEW_COUNT
  };

  SubGhzModule();

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

  SubView _view = VIEW_WATERFALL;
  SubGhzBand _band = BAND_433_MHZ;
  float _freqMhz = 433.92f;
  int _lastRssiDbm = -105;
  int _peakRssiDbm = -105;

  // Compact ring/row waterfall stored as color rows (128x48 RGB565 is ~12 KB, pushed via 16px strips)
  uint16_t _waterfall[SUBGHZ_WF_ROWS][SUBGHZ_WF_COLS];
  size_t _wfHead = 0;
  uint32_t _lastSampleMs = 0;

  // Raw pulse capture & controlled own-device replay buffer (microseconds)
  uint32_t _pulsesUs[SUBGHZ_MAX_PULSES];
  size_t _pulseCount = 0;
  bool _capturing = false;
  uint32_t _lastEdgeUs = 0;
  int _lastPinState = LOW;
  char _statusMsg[32];

  Module* _rlMod = nullptr;
  CC1101* _cc1101 = nullptr;

  bool initRadioForBand(SubGhzBand band);
  void releaseRadio();
  float bandToFreq(SubGhzBand band) const;
  const char* bandToLabel(SubGhzBand band) const;
  uint16_t rssiToColor(int rssiDbm) const;
  int sampleRssiLocked();
  void pushWaterfallRow(int rssiDbm);

  void togglePulseCapture();
  void pollPulseCapture();
  bool savePulsesToSd();
  void replayRecordedPulses();

  void drawHeaderStrip(const char* title, const char* rightHint);
  void drawWaterfallView();
  void drawCaptureReplayView();
};
