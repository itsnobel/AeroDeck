#pragma once

#include <Arduino.h>
#include <SD.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

#ifndef PIN_IR_RX
#define PIN_IR_RX 3
#endif

#ifndef PIN_IR_TX
#define PIN_IR_TX 13
#endif

static constexpr uint16_t IR_RAW_BUF_SIZE = 120;
static constexpr uint16_t IR_MAX_RAW_SAMPLES = 256;
static constexpr uint16_t IR_CAPTURE_TIMEOUT_MS = 35;

class IrLabModule {
public:
  enum SubView : uint8_t {
    VIEW_RX_DECODE = 0,
    VIEW_TX_REPLAY,
    VIEW_COUNT
  };

  IrLabModule();

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
  bool _rxStarted = false;
  bool _txStarted = false;

  SubView _view = VIEW_RX_DECODE;
  IRrecv* _irRecv = nullptr;
  IRsend* _irSend = nullptr;
  decode_results _results;

  char _protoName[16];
  char _codeHex[20];
  uint16_t _bitsCount = 0;
  uint16_t _rawSamples[IR_MAX_RAW_SAMPLES];
  uint16_t _rawCount = 0;
  uint16_t _carrierKhz = 38;
  uint32_t _capturesSeen = 0;
  char _statusLine[32];

  void ensureRxStarted();
  void stopRx();
  void ensureTxStarted();
  void pollIrReceiver();
  bool saveCaptureToSd();
  void replayLastCapture();

  void drawHeaderStrip(const char* title, const char* rightHint);
  void drawRxDecodeView();
  void drawTxReplayView();
};
