#include "mod_ir.h"
#include <string.h>
#include <stdio.h>

IrLabModule::IrLabModule() {
  snprintf(_protoName, sizeof(_protoName), "NONE");
  snprintf(_codeHex, sizeof(_codeHex), "0x0");
  snprintf(_statusLine, sizeof(_statusLine), "Point IR at VS1838B");
  memset(_rawSamples, 0, sizeof(_rawSamples));
}

void IrLabModule::ensureRxStarted() {
  if (_rxStarted) return;
  pinMode(PIN_IR_RX, INPUT);
  if (!_irRecv) {
    _irRecv = new IRrecv(PIN_IR_RX, IR_RAW_BUF_SIZE, IR_CAPTURE_TIMEOUT_MS, true);
  }
  _irRecv->enableIRIn();
  _rxStarted = true;
}

void IrLabModule::stopRx() {
  if (!_rxStarted) return;
  if (_irRecv) {
    _irRecv->disableIRIn();
  }
  _rxStarted = false;
}

void IrLabModule::ensureTxStarted() {
  if (_txStarted) return;
  // Ensure GPIO13 is not held by USB-Serial/JTAG before RMT TX initialization
  pinMode(PIN_IR_TX, OUTPUT);
  digitalWrite(PIN_IR_TX, LOW);
  if (!_irSend) {
    _irSend = new IRsend(PIN_IR_TX);
  }
  _irSend->begin();
  _txStarted = true;
}

void IrLabModule::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _active = true;
  _view = VIEW_RX_DECODE;
  ensureRxStarted();
}

void IrLabModule::exit() {
  stopRx();
  if (_irSend) {
    delete _irSend;
    _irSend = nullptr;
  }
  _txStarted = false;
  if (_irRecv) {
    delete _irRecv;
    _irRecv = nullptr;
  }
  _active = false;
}

void IrLabModule::pollIrReceiver() {
  if (!_rxStarted || !_irRecv) return;

  if (_irRecv->decode(&_results)) {
    _capturesSeen++;
    _bitsCount = _results.bits;
    _carrierKhz = 38;

    if (_results.decode_type == NEC) {
      snprintf(_protoName, sizeof(_protoName), "NEC");
    } else if (_results.decode_type == SONY) {
      snprintf(_protoName, sizeof(_protoName), "SONY");
      _carrierKhz = 40;
    } else if (_results.decode_type == RC5) {
      snprintf(_protoName, sizeof(_protoName), "RC5");
      _carrierKhz = 36;
    } else if (_results.decode_type == UNKNOWN) {
      snprintf(_protoName, sizeof(_protoName), "RAW");
    } else {
      const char* pStr = typeToString(_results.decode_type, _results.repeat);
      snprintf(_protoName, sizeof(_protoName), "%.14s", pStr ? pStr : "OTHER");
    }

    snprintf(_codeHex, sizeof(_codeHex), "0x%llX", static_cast<unsigned long long>(_results.value));

    // Extract raw microsecond mark/space intervals (excluding leading gap if oversized)
    _rawCount = 0;
    if (_results.rawlen > 1) {
      uint16_t startIdx = 1;
      for (uint16_t i = startIdx; i < _results.rawlen && _rawCount < IR_MAX_RAW_SAMPLES; ++i) {
        uint32_t us = static_cast<uint32_t>(_results.rawbuf[i]) * kRawTick;
        if (us > 65000UL) us = 65000UL;
        _rawSamples[_rawCount++] = static_cast<uint16_t>(us);
      }
    }

    snprintf(_statusLine, sizeof(_statusLine), "RX #%lu (%u samples)",
             static_cast<unsigned long>(_capturesSeen),
             static_cast<unsigned>(_rawCount));

    _irRecv->resume();
  }
}

bool IrLabModule::saveCaptureToSd() {
  if (!_bus || _rawCount == 0) return false;

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
  snprintf(path, sizeof(path), "/aerodeck/ir_%lu.txt", static_cast<unsigned long>(millis() / 1000));
  File f = SD.open(path, FILE_WRITE);
  bool ok = false;
  if (f) {
    f.printf("PROTO=%s\nCODE=%s\nBITS=%u\nKHZ=%u\nCOUNT=%u\n",
             _protoName, _codeHex,
             static_cast<unsigned>(_bitsCount),
             static_cast<unsigned>(_carrierKhz),
             static_cast<unsigned>(_rawCount));
    for (uint16_t i = 0; i < _rawCount; ++i) {
      f.printf("%u%s", static_cast<unsigned>(_rawSamples[i]), (i + 1 < _rawCount) ? "," : "\n");
    }
    f.close();
    ok = true;
  }
  _bus->releaseAllCs();
  return ok;
}

void IrLabModule::replayLastCapture() {
  if (_rawCount == 0) {
    snprintf(_statusLine, sizeof(_statusLine), "No IR capture in RAM");
    return;
  }

  // Pause RX during TX so carrier reflection does not pollute receiver buffer
  stopRx();
  ensureTxStarted();

  if (_gui) {
    _gui->drawToast("IR TX", "Sending profile...", TFT_RED, 250);
  }

  _irSend->sendRaw(_rawSamples, _rawCount, _carrierKhz);
  snprintf(_statusLine, sizeof(_statusLine), "Sent %s (%u)", _protoName, static_cast<unsigned>(_rawCount));

  if (_view == VIEW_RX_DECODE) {
    ensureRxStarted();
  }
}

void IrLabModule::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);
    if (_view == VIEW_RX_DECODE) {
      ensureRxStarted();
    } else if (_view == VIEW_TX_REPLAY) {
      stopRx();
      ensureTxStarted();
    }
    return;
  }

  if (_view == VIEW_RX_DECODE) {
    if (ev == BTN_EVT_SELECT) {
      if (saveCaptureToSd()) {
        snprintf(_statusLine, sizeof(_statusLine), "Saved IR to /aerodeck");
      } else {
        snprintf(_statusLine, sizeof(_statusLine), "SD save failed/empty");
      }
    } else if (ev == BTN_EVT_UP) {
      replayLastCapture();
    }
  } else if (_view == VIEW_TX_REPLAY) {
    if (ev == BTN_EVT_SELECT) {
      replayLastCapture();
    } else if (ev == BTN_EVT_UP) {
      _carrierKhz = (_carrierKhz == 38) ? 40 : ((_carrierKhz == 40) ? 36 : 38);
      snprintf(_statusLine, sizeof(_statusLine), "Carrier: %u kHz", static_cast<unsigned>(_carrierKhz));
    }
  }
}

void IrLabModule::tick() {
  if (_view == VIEW_RX_DECODE && _rxStarted) {
    pollIrReceiver();
  }
}

void IrLabModule::drawHeaderStrip(const char* title, const char* rightHint) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_RED, 0x1082);
  _gui->strip().drawString(title ? title : "IR LAB", 4, 8);
  if (rightHint && rightHint[0] != '\0') {
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
    _gui->strip().drawString(rightHint, TFT_W - 4, 8);
  }
  _gui->pushStrip(0, BODY_Y_START);
}

void IrLabModule::drawRxDecodeView() {
  drawHeaderStrip("IR RX DECODE", "R:Tab");

  if (_gui->beginStrip(36, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char pLine[32];
    snprintf(pLine, sizeof(pLine), "Proto: %s (%u b)", _protoName, static_cast<unsigned>(_bitsCount));
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(pLine, 4, 8);

    char cLine[32];
    snprintf(cLine, sizeof(cLine), "Code: %s", _codeHex);
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(cLine, 4, 22);
    _gui->pushStrip(0, 36);
  }

  if (_gui->beginStrip(72, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_statusLine, 4, 14);
    _gui->pushStrip(0, 72);
  }

  if (_gui->beginStrip(108, 30, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString("SEL:SaveSD  UP:Replay", 4, 12);
    _gui->pushStrip(0, 108);
  }
}

void IrLabModule::drawTxReplayView() {
  drawHeaderStrip("IR TX REPLAY", "R:Tab");

  if (_gui->beginStrip(38, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char info1[32];
    snprintf(info1, sizeof(info1), "Signal: %s %s", _protoName, _codeHex);
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(info1, 4, 8);

    char info2[32];
    snprintf(info2, sizeof(info2), "Samples:%u  %ukHz",
             static_cast<unsigned>(_rawCount), static_cast<unsigned>(_carrierKhz));
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(info2, 4, 22);
    _gui->pushStrip(0, 38);
  }

  if (_gui->beginStrip(74, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_statusLine, 4, 8);
    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString("SEL:Send  UP:kHz", 4, 22);
    _gui->pushStrip(0, 74);
  }
}

void IrLabModule::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_RX_DECODE: drawRxDecodeView(); break;
    case VIEW_TX_REPLAY: drawTxReplayView(); break;
    default:             drawRxDecodeView(); break;
  }
}
