#include "mod_nrf24.h"
#include <string.h>
#include <stdio.h>

Nrf24Module::Nrf24Module() : _radio(PIN_NRF24_CE, PIN_NRF24_CSN) {
  memset(_chHits, 0, sizeof(_chHits));
  memset(_waterfall, 0, sizeof(_waterfall));
  snprintf(_statusText, sizeof(_statusText), "Spectrum sweep active");
}

uint16_t Nrf24Module::hitLevelToColor(uint16_t hits, uint16_t maxHits) const {
  if (hits == 0) return TFT_NAVY;
  uint32_t ratio100 = (static_cast<uint32_t>(hits) * 100U) / (maxHits > 0 ? maxHits : 1U);
  if (ratio100 < 15) return TFT_BLUE;
  if (ratio100 < 35) return TFT_CYAN;
  if (ratio100 < 60) return TFT_GREEN;
  if (ratio100 < 85) return TFT_YELLOW;
  return TFT_RED;
}

bool Nrf24Module::initRadioLocked() {
  if (!_bus) return false;
  SpiGuard lock(*_bus);
  if (!lock.acquired()) return false;

  _bus->selectOnlyCs(PIN_NRF24_CSN);
  bool ok = _radio.begin(&SPI);
  if (ok) {
    _radio.setAutoAck(false);
    _radio.setDynamicPayloadSize(true);
    _radio.setDataRate(RF24_1MBPS);
    _radio.setPALevel(RF24_PA_MIN);
    _radio.stopListening();
  }
  _bus->releaseAllCs();

  _radioReady = ok;
  if (!ok) {
    snprintf(_statusText, sizeof(_statusText), "NRF24 init failed");
  }
  return ok;
}

void Nrf24Module::powerDownRadioLocked() {
  if (!_bus) return;
  SpiGuard lock(*_bus);
  if (!lock.acquired()) return;

  _bus->selectOnlyCs(PIN_NRF24_CSN);
  if (_radioReady) {
    _radio.stopListening();
    _radio.powerDown();
  }
  _bus->releaseAllCs();
  _radioReady = false;
  _sniffEnabled = false;
}

void Nrf24Module::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _active = true;
  _view = VIEW_SPECTRUM_WF;
  _sniffEnabled = false;

  memset(_chHits, 0, sizeof(_chHits));
  memset(_waterfall, 0, sizeof(_waterfall));
  _maxChHits = 1;
  _peakChannel = 0;
  _wfHead = 0;

  initRadioLocked();
}

void Nrf24Module::exit() {
  powerDownRadioLocked();
  _active = false;
}

void Nrf24Module::sweepSpectrumLocked() {
  if (!_bus || !_radioReady) return;

  static constexpr uint8_t SAMPLES_PER_CH = 8;
  uint16_t frameHits[NRF24_NUM_CHANNELS] = {0};
  uint16_t localPeakHits = 0;
  uint8_t localPeakCh = 0;

  {
    SpiGuard lock(*_bus);
    if (!lock.acquired()) return;

    _bus->selectOnlyCs(PIN_NRF24_CSN);
    _radio.stopListening();

    for (uint8_t ch = 0; ch < NRF24_NUM_CHANNELS; ++ch) {
      _radio.setChannel(ch);
      uint16_t hits = 0;
      for (uint8_t s = 0; s < SAMPLES_PER_CH; ++s) {
        _radio.startListening();
        delayMicroseconds(130);
        bool carrier = _radio.testCarrier() || _radio.testRPD();
        _radio.stopListening();
        if (carrier) {
          hits++;
        }
      }
      frameHits[ch] = hits;
      _chHits[ch] += hits;
      if (_chHits[ch] > _maxChHits) {
        _maxChHits = _chHits[ch];
      }
      if (hits >= localPeakHits) {
        localPeakHits = hits;
        localPeakCh = ch;
      }
    }
    _bus->releaseAllCs();
  }

  _peakChannel = localPeakCh;
  size_t row = _wfHead;
  for (uint8_t ch = 0; ch < NRF24_NUM_CHANNELS; ++ch) {
    _waterfall[row][ch] = hitLevelToColor(frameHits[ch], SAMPLES_PER_CH);
  }
  _wfHead = (_wfHead + 1) % NRF24_WF_ROWS;
}

void Nrf24Module::pollPassiveRxLocked() {
  if (!_bus || !_radioReady || !_sniffEnabled) return;

  SpiGuard lock(*_bus);
  if (!lock.acquired()) return;

  _bus->selectOnlyCs(PIN_NRF24_CSN);
  while (_radio.available()) {
    uint8_t len = _radio.getDynamicPayloadSize();
    if (len > 32) len = 32;
    uint8_t buf[32];
    if (len > 0) {
      _radio.read(buf, len);
    }
    _rxPacketsSeen++;
    _crcOkPackets++;
    _payloadBytesSeen += len;
  }
  _bus->releaseAllCs();
}

void Nrf24Module::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);

    if (_view == VIEW_PASSIVE_STATS && _radioReady && _bus) {
      SpiGuard lock(*_bus);
      if (lock.acquired()) {
        _bus->selectOnlyCs(PIN_NRF24_CSN);
        _radio.setChannel(_monitorChannel);
        _radio.startListening();
        _sniffEnabled = true;
        _bus->releaseAllCs();
      }
      snprintf(_statusText, sizeof(_statusText), "RX ch %u (%uM)",
               static_cast<unsigned>(_monitorChannel),
               static_cast<unsigned>(2400 + _monitorChannel));
    } else if (_view == VIEW_SPECTRUM_WF && _radioReady && _bus) {
      SpiGuard lock(*_bus);
      if (lock.acquired()) {
        _bus->selectOnlyCs(PIN_NRF24_CSN);
        _radio.stopListening();
        _sniffEnabled = false;
        _bus->releaseAllCs();
      }
      snprintf(_statusText, sizeof(_statusText), "Spectrum sweep active");
    }
    return;
  }

  if (_view == VIEW_SPECTRUM_WF && ev == BTN_EVT_SELECT) {
    memset(_chHits, 0, sizeof(_chHits));
    memset(_waterfall, 0, sizeof(_waterfall));
    _maxChHits = 1;
    _wfHead = 0;
    snprintf(_statusText, sizeof(_statusText), "Spectrum cleared");
  } else if (_view == VIEW_PASSIVE_STATS) {
    if (ev == BTN_EVT_UP) {
      _monitorChannel = (_monitorChannel + 5) % NRF24_NUM_CHANNELS;
    } else if (ev == BTN_EVT_DOWN) {
      _monitorChannel = (_monitorChannel >= 5) ? (_monitorChannel - 5) : (NRF24_NUM_CHANNELS - 5 + _monitorChannel);
    } else if (ev == BTN_EVT_SELECT) {
      _rxPacketsSeen = 0;
      _crcOkPackets = 0;
      _payloadBytesSeen = 0;
    }

    if (_radioReady && _bus) {
      SpiGuard lock(*_bus);
      if (lock.acquired()) {
        _bus->selectOnlyCs(PIN_NRF24_CSN);
        _radio.setChannel(_monitorChannel);
        _radio.startListening();
        _bus->releaseAllCs();
      }
    }
    snprintf(_statusText, sizeof(_statusText), "RX ch %u (%uM)",
             static_cast<unsigned>(_monitorChannel),
             static_cast<unsigned>(2400 + _monitorChannel));
  }
}

void Nrf24Module::tick() {
  if (!_radioReady) return;

  if (_view == VIEW_SPECTRUM_WF) {
    uint32_t now = millis();
    if (now - _lastSweepMs >= 180) {
      _lastSweepMs = now;
      sweepSpectrumLocked();
    }
  } else if (_view == VIEW_PASSIVE_STATS) {
    pollPassiveRxLocked();
  }
}

void Nrf24Module::drawHeaderStrip(const char* title, const char* rightHint) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_MAGENTA, 0x1082);
  _gui->strip().drawString(title ? title : "2.4G", 4, 8);
  if (rightHint && rightHint[0] != '\0') {
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
    _gui->strip().drawString(rightHint, TFT_W - 4, 8);
  }
  _gui->pushStrip(0, BODY_Y_START);
}

void Nrf24Module::drawSpectrumView() {
  char rHint[18];
  snprintf(rHint, sizeof(rHint), "Pk:%uM R:Tab", static_cast<unsigned>(2400 + _peakChannel));
  drawHeaderStrip("2.4G SPEC", rHint);

  if (_gui->beginStrip(34, 14, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString("2400MHz", 3, 7);
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().drawString("2525MHz", TFT_W - 3, 7);
    _gui->pushStrip(0, 34);
  }

  static constexpr int16_t WF_TOP = 48;
  for (int stripIdx = 0; stripIdx < 4; ++stripIdx) {
    int16_t sy = WF_TOP + stripIdx * 24;
    int16_t sh = 24;
    if (sy + sh > BODY_Y_END) {
      sh = BODY_Y_END - sy;
    }
    if (sh <= 0) continue;

    if (_gui->beginStrip(sy, sh, TFT_BLACK)) {
      for (int16_t ly = 0; ly < sh; ++ly) {
        size_t age = static_cast<size_t>(stripIdx * 24 + ly);
        size_t rIdx = (_wfHead + NRF24_WF_ROWS - 1 - age) % NRF24_WF_ROWS;
        for (int16_t x = 0; x < TFT_W; ++x) {
          uint8_t ch = static_cast<uint8_t>((static_cast<uint32_t>(x) * NRF24_NUM_CHANNELS) / TFT_W);
          if (ch >= NRF24_NUM_CHANNELS) ch = NRF24_NUM_CHANNELS - 1;
          _gui->strip().drawPixel(x, ly, _waterfall[rIdx][ch]);
        }
      }
      _gui->pushStrip(0, sy);
    }
  }
}

void Nrf24Module::drawPassiveStatsView() {
  drawHeaderStrip("PASSIVE 2.4G RX", "R:Tab");

  if (_gui->beginStrip(38, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_statusText, 4, 8);

    char b1[32];
    snprintf(b1, sizeof(b1), "RX Frames: %lu", static_cast<unsigned long>(_rxPacketsSeen));
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(b1, 4, 22);
    _gui->pushStrip(0, 38);
  }

  if (_gui->beginStrip(74, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char b2[32];
    snprintf(b2, sizeof(b2), "Payload: %lu B", static_cast<unsigned long>(_payloadBytesSeen));
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(b2, 4, 8);

    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString("U/D:Ch+-5 SEL:Clr", 4, 22);
    _gui->pushStrip(0, 74);
  }
}

void Nrf24Module::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_SPECTRUM_WF:  drawSpectrumView(); break;
    case VIEW_PASSIVE_STATS: drawPassiveStatsView(); break;
    default:                 drawSpectrumView(); break;
  }
}
