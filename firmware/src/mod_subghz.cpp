#include "mod_subghz.h"
#include <string.h>
#include <stdio.h>

SubGhzModule::SubGhzModule() {
  memset(_waterfall, 0, sizeof(_waterfall));
  memset(_pulsesUs, 0, sizeof(_pulsesUs));
  snprintf(_statusMsg, sizeof(_statusMsg), "Idle (SEL:Cap/Rpl)");
}

float SubGhzModule::bandToFreq(SubGhzBand band) const {
  switch (band) {
    case BAND_315_MHZ: return 315.00f;
    case BAND_433_MHZ: return 433.92f;
    case BAND_868_MHZ: return 868.35f;
    case BAND_915_MHZ: return 915.00f;
    default:           return 433.92f;
  }
}

const char* SubGhzModule::bandToLabel(SubGhzBand band) const {
  switch (band) {
    case BAND_315_MHZ: return "315M";
    case BAND_433_MHZ: return "433.9M";
    case BAND_868_MHZ: return "868.3M";
    case BAND_915_MHZ: return "915M";
    default:           return "433.9M";
  }
}

uint16_t SubGhzModule::rssiToColor(int rssiDbm) const {
  if (rssiDbm < -92) return TFT_NAVY;
  if (rssiDbm < -82) return TFT_BLUE;
  if (rssiDbm < -70) return TFT_CYAN;
  if (rssiDbm < -58) return TFT_GREEN;
  if (rssiDbm < -45) return TFT_YELLOW;
  return TFT_RED;
}

void SubGhzModule::releaseRadio() {
  if (!_bus) return;
  SpiGuard lock(*_bus);
  if (lock.acquired()) {
    _bus->selectOnlyCs(PIN_CC1101_CSN);
    if (_cc1101) {
      _cc1101->sleep();
    }
    _bus->releaseAllCs();
  }
  if (_cc1101) {
    delete _cc1101;
    _cc1101 = nullptr;
  }
  if (_rlMod) {
    delete _rlMod;
    _rlMod = nullptr;
  }
  _radioReady = false;
}

bool SubGhzModule::initRadioForBand(SubGhzBand band) {
  if (!_bus) return false;
  releaseRadio();

  _band = band;
  _freqMhz = bandToFreq(band);
  _peakRssiDbm = -110;

  SpiGuard lock(*_bus);
  if (!lock.acquired()) return false;

  _bus->selectOnlyCs(PIN_CC1101_CSN);
  _rlMod = new Module(PIN_CC1101_CSN, PIN_CC1101_GDO0, RADIOLIB_NC, RADIOLIB_NC, SPI);
  _cc1101 = new CC1101(_rlMod);

  int16_t state = _cc1101->begin(_freqMhz, 4.8f, 5.0f, 100.0f, 10.0f, 16, 0, false);
  if (state != RADIOLIB_ERR_NONE) {
    _radioReady = false;
    _bus->releaseAllCs();
    snprintf(_statusMsg, sizeof(_statusMsg), "CC1101 err:%d", static_cast<int>(state));
    return false;
  }

  _cc1101->setOOK(true);
  _cc1101->receiveDirect();
  _radioReady = true;
  _bus->releaseAllCs();
  snprintf(_statusMsg, sizeof(_statusMsg), "Tuned %s", bandToLabel(_band));
  return true;
}

void SubGhzModule::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _active = true;
  _view = VIEW_WATERFALL;
  _capturing = false;
  memset(_waterfall, 0, sizeof(_waterfall));
  _wfHead = 0;

  initRadioForBand(_band);
}

void SubGhzModule::exit() {
  _capturing = false;
  releaseRadio();
  _active = false;
}

int SubGhzModule::sampleRssiLocked() {
  if (!_bus || !_radioReady || !_cc1101) return -108;
  SpiGuard lock(*_bus);
  if (!lock.acquired()) return _lastRssiDbm;

  _bus->selectOnlyCs(PIN_CC1101_CSN);
  float rssi = _cc1101->getRSSI();
  _bus->releaseAllCs();

  int val = static_cast<int>(rssi);
  if (val < -115) val = -115;
  if (val > -10) val = -10;
  _lastRssiDbm = val;
  if (val > _peakRssiDbm) {
    _peakRssiDbm = val;
  }
  return val;
}

void SubGhzModule::pushWaterfallRow(int rssiDbm) {
  uint16_t col = rssiToColor(rssiDbm);
  size_t row = _wfHead;
  for (size_t x = 0; x < SUBGHZ_WF_COLS - 1; ++x) {
    _waterfall[row][x] = _waterfall[row][x + 1];
  }
  _waterfall[row][SUBGHZ_WF_COLS - 1] = col;
  _wfHead = (_wfHead + 1) % SUBGHZ_WF_ROWS;
}

void SubGhzModule::togglePulseCapture() {
  if (!_radioReady || !_cc1101 || !_bus) return;

  if (!_capturing) {
    _pulseCount = 0;
    _lastPinState = digitalRead(PIN_CC1101_GDO0);
    _lastEdgeUs = micros();
    _capturing = true;
    snprintf(_statusMsg, sizeof(_statusMsg), "Capturing pulses...");
  } else {
    _capturing = false;
    snprintf(_statusMsg, sizeof(_statusMsg), "Cap:%u edges", static_cast<unsigned>(_pulseCount));
    savePulsesToSd();
  }
}

void SubGhzModule::pollPulseCapture() {
  if (!_capturing) return;
  int cur = digitalRead(PIN_CC1101_GDO0);
  uint32_t nowUs = micros();
  if (cur != _lastPinState) {
    uint32_t dur = nowUs - _lastEdgeUs;
    if (dur >= 50 && dur <= 100000 && _pulseCount < SUBGHZ_MAX_PULSES) {
      _pulsesUs[_pulseCount++] = dur;
    }
    _lastPinState = cur;
    _lastEdgeUs = nowUs;
  }
  if (_pulseCount >= SUBGHZ_MAX_PULSES) {
    _capturing = false;
    snprintf(_statusMsg, sizeof(_statusMsg), "Buffer full (%u)", static_cast<unsigned>(_pulseCount));
    savePulsesToSd();
  }
}

bool SubGhzModule::savePulsesToSd() {
  if (!_bus || _pulseCount == 0) return false;
  SpiGuard lock(*_bus);
  if (!lock.acquired()) return false;

  _bus->selectOnlyCs(PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI, 10000000)) {
    _bus->releaseAllCs();
    return false;
  }
  if (!SD.exists("/aerodeck")) {
    SD.mkdir("/aerodeck");
  }

  char path[40];
  snprintf(path, sizeof(path), "/aerodeck/sub_%s_%lu.txt",
           bandToLabel(_band), static_cast<unsigned long>(millis() / 1000));
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    f.printf("FREQ_MHZ=%.2f\nCOUNT=%u\n", _freqMhz, static_cast<unsigned>(_pulseCount));
    for (size_t i = 0; i < _pulseCount; ++i) {
      f.printf("%lu%s", static_cast<unsigned long>(_pulsesUs[i]), (i + 1 < _pulseCount) ? "," : "\n");
    }
    f.close();
  }
  _bus->releaseAllCs();
  return true;
}

void SubGhzModule::replayRecordedPulses() {
  if (!_bus || !_radioReady || !_cc1101 || _pulseCount == 0 || _capturing) return;

  if (_gui) {
    _gui->drawToast("SUB-GHZ TX", "Replaying bench log", TFT_ORANGE, 350);
  }

  {
    SpiGuard lock(*_bus);
    if (!lock.acquired()) return;
    _bus->selectOnlyCs(PIN_CC1101_CSN);
    _cc1101->transmitDirect();
    _bus->releaseAllCs();
  }

  pinMode(PIN_CC1101_GDO0, OUTPUT);
  bool level = HIGH;
  for (size_t i = 0; i < _pulseCount; ++i) {
    digitalWrite(PIN_CC1101_GDO0, level ? HIGH : LOW);
    delayMicroseconds(_pulsesUs[i]);
    level = !level;
  }
  digitalWrite(PIN_CC1101_GDO0, LOW);
  pinMode(PIN_CC1101_GDO0, INPUT);

  {
    SpiGuard lock(*_bus);
    if (lock.acquired()) {
      _bus->selectOnlyCs(PIN_CC1101_CSN);
      _cc1101->receiveDirect();
      _bus->releaseAllCs();
    }
  }

  snprintf(_statusMsg, sizeof(_statusMsg), "Replayed %u", static_cast<unsigned>(_pulseCount));
}

void SubGhzModule::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    _capturing = false;
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);
    return;
  }

  if (ev == BTN_EVT_DOWN) {
    int nextB = (static_cast<int>(_band) + 1) % static_cast<int>(BAND_COUNT);
    initRadioForBand(static_cast<SubGhzBand>(nextB));
    return;
  } else if (ev == BTN_EVT_UP) {
    int prevB = (_band == 0) ? (static_cast<int>(BAND_COUNT) - 1) : (static_cast<int>(_band) - 1);
    initRadioForBand(static_cast<SubGhzBand>(prevB));
    return;
  }

  if (_view == VIEW_CAPTURE_REPLAY) {
    if (ev == BTN_EVT_SELECT) {
      togglePulseCapture();
    } else if (ev == BTN_EVT_LEFT) {
      replayRecordedPulses();
    }
  }
}

void SubGhzModule::tick() {
  uint32_t now = millis();
  if (now - _lastSampleMs >= 60) {
    _lastSampleMs = now;
    int rssi = sampleRssiLocked();
    pushWaterfallRow(rssi);
  }
  pollPulseCapture();
}

void SubGhzModule::drawHeaderStrip(const char* title, const char* rightHint) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_ORANGE, 0x1082);
  _gui->strip().drawString(title ? title : "SUB-GHZ", 4, 8);
  if (rightHint && rightHint[0] != '\0') {
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
    _gui->strip().drawString(rightHint, TFT_W - 4, 8);
  }
  _gui->pushStrip(0, BODY_Y_START);
}

void SubGhzModule::drawWaterfallView() {
  char rHint[16];
  snprintf(rHint, sizeof(rHint), "%s R:Tab", bandToLabel(_band));
  drawHeaderStrip("CC1101 WF", rHint);

  // RSSI readout & meter strip (y = 34..53)
  if (_gui->beginStrip(34, 20, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char buf[28];
    snprintf(buf, sizeof(buf), "RSSI:%d Pk:%d", _lastRssiDbm, _peakRssiDbm);
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(buf, 4, 5);
    _gui->drawSignalMeter(4, 11, TFT_W - 8, 7, _lastRssiDbm, TFT_DARKGREY);
    _gui->pushStrip(0, 34);
  }

  // Waterfall rows rendered in two 24px strips (y = 54..101 and y = 102..149 -> clipped to 144)
  static constexpr int16_t WF_TOP = 54;
  for (int stripIdx = 0; stripIdx < 2; ++stripIdx) {
    int16_t sy = WF_TOP + stripIdx * 24;
    int16_t sh = 24;
    if (sy + sh > BODY_Y_END) {
      sh = BODY_Y_END - sy;
    }
    if (sh <= 0) continue;

    if (_gui->beginStrip(sy, sh, TFT_BLACK)) {
      for (int16_t ly = 0; ly < sh; ++ly) {
        size_t age = static_cast<size_t>(stripIdx * 24 + ly);
        size_t rIdx = (_wfHead + SUBGHZ_WF_ROWS - 1 - age) % SUBGHZ_WF_ROWS;
        for (int16_t x = 0; x < TFT_W; ++x) {
          _gui->strip().drawPixel(x, ly, _waterfall[rIdx][x]);
        }
      }
      _gui->pushStrip(0, sy);
    }
  }
}

void SubGhzModule::drawCaptureReplayView() {
  drawHeaderStrip("SUB-GHZ LAB", bandToLabel(_band));

  if (_gui->beginStrip(38, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_statusMsg, 4, 14);
    _gui->pushStrip(0, 38);
  }

  if (_gui->beginStrip(74, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char buf[32];
    snprintf(buf, sizeof(buf), "Samples: %u / %u",
             static_cast<unsigned>(_pulseCount), static_cast<unsigned>(SUBGHZ_MAX_PULSES));
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(buf, 4, 14);
    _gui->pushStrip(0, 74);
  }

  if (_gui->beginStrip(110, 30, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString("SEL:Cap/Save  L:Replay", 4, 12);
    _gui->pushStrip(0, 110);
  }
}

void SubGhzModule::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_WATERFALL:      drawWaterfallView(); break;
    case VIEW_CAPTURE_REPLAY: drawCaptureReplayView(); break;
    default:                  drawWaterfallView(); break;
  }
}
