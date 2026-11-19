#include "mod_gps.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef PIN_GPS_RX_FROM_TX
#define PIN_GPS_RX_FROM_TX 0
#endif

#ifndef PIN_GPS_TX_TO_RX
#define PIN_GPS_TX_TO_RX 1
#endif

GpsModule::GpsModule() : _gpsSerial(1) {}

void GpsModule::ensureUartStarted() {
  if (_uartStarted) return;
  _gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX_FROM_TX, PIN_GPS_TX_TO_RX);
  _uartStarted = true;
  configure5HzRate();
}

void GpsModule::sendUbxMessage(uint8_t msgClass, uint8_t msgId, const uint8_t* payload, uint16_t len) {
  if (!_uartStarted) return;

  uint8_t ckA = 0;
  uint8_t ckB = 0;

  auto addChk = [&](uint8_t b) {
    ckA = static_cast<uint8_t>(ckA + b);
    ckB = static_cast<uint8_t>(ckB + ckA);
  };

  _gpsSerial.write(0xB5);
  _gpsSerial.write(0x62);

  _gpsSerial.write(msgClass);
  addChk(msgClass);

  _gpsSerial.write(msgId);
  addChk(msgId);

  uint8_t lenLo = static_cast<uint8_t>(len & 0xFF);
  uint8_t lenHi = static_cast<uint8_t>((len >> 8) & 0xFF);
  _gpsSerial.write(lenLo);
  addChk(lenLo);
  _gpsSerial.write(lenHi);
  addChk(lenHi);

  for (uint16_t i = 0; i < len; ++i) {
    uint8_t b = payload ? payload[i] : 0;
    _gpsSerial.write(b);
    addChk(b);
  }

  _gpsSerial.write(ckA);
  _gpsSerial.write(ckB);
  _gpsSerial.flush();
}

void GpsModule::configure5HzRate() {
  // UBX-CFG-RATE (0x06, 0x08): measRate = 200 ms (5 Hz), navRate = 1, timeRef = 1 (GPS)
  const uint8_t cfgRate5Hz[6] = {
    0xC8, 0x00, // measRate = 200 ms (little-endian)
    0x01, 0x00, // navRate = 1
    0x01, 0x00  // timeRef = GPS time
  };
  sendUbxMessage(0x06, 0x08, cfgRate5Hz, sizeof(cfgRate5Hz));
}

void GpsModule::configurePowerSave(bool enable) {
  // UBX-CFG-RXM (0x06, 0x11): reserved=0, lpMode = 1 (Power Save) or 0 (Continuous)
  const uint8_t cfgRxm[2] = {
    0x00,
    static_cast<uint8_t>(enable ? 0x01 : 0x00)
  };
  sendUbxMessage(0x06, 0x11, cfgRxm, sizeof(cfgRxm));
  _powerSaveMode = enable;
}

void GpsModule::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _active = true;
  _view = VIEW_SPEEDO;
  ensureUartStarted();
}

void GpsModule::exit() {
  closeGeoLogs();
  _active = false;
}

bool GpsModule::hasFix() const {
  return _gps.location.isValid() && (_gps.location.age() < 3000);
}

uint8_t GpsModule::satellites() const {
  return _gps.satellites.isValid() ? static_cast<uint8_t>(_gps.satellites.value()) : 0;
}

double GpsModule::latitude() const {
  return _gps.location.isValid() ? _gps.location.lat() : 0.0;
}

double GpsModule::longitude() const {
  return _gps.location.isValid() ? _gps.location.lng() : 0.0;
}

float GpsModule::speedKmh() const {
  if (!hasFix() || !_gps.speed.isValid()) return 0.0f;
  float kmh = _gps.speed.kmph();
  return (kmh < 0.8f) ? 0.0f : kmh; // Noise clamp when stationary
}

void GpsModule::updateTripMetrics() {
  if (!hasFix()) return;

  float spd = speedKmh();
  if (spd > _maxSpeedKmh) {
    _maxSpeedKmh = spd;
  }

  double lat = _gps.location.lat();
  double lon = _gps.location.lng();
  if (_hasPrevCoord) {
    double dMeters = TinyGPSPlus::distanceBetween(_prevLat, _prevLon, lat, lon);
    if (dMeters >= 1.5 && dMeters < 500.0) {
      _tripMeters += dMeters;
      _prevLat = lat;
      _prevLon = lon;
    }
  } else {
    _prevLat = lat;
    _prevLon = lon;
    _hasPrevCoord = true;
  }
}

bool GpsModule::openGeoLogs() {
  if (!_bus) return false;
  closeGeoLogs();

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

  uint32_t tag = millis() / 1000;
  char csvPath[40];
  char kmlPath[40];
  snprintf(csvPath, sizeof(csvPath), "/aerodeck/gps_%lu.csv", static_cast<unsigned long>(tag));
  snprintf(kmlPath, sizeof(kmlPath), "/aerodeck/gps_%lu.kml", static_cast<unsigned long>(tag));

  _csvFile = SD.open(csvPath, FILE_WRITE);
  _kmlFile = SD.open(kmlPath, FILE_WRITE);

  if (_csvFile) {
    _csvFile.println("lat,lon,alt_m,speed_kmh,sats,millis");
    _csvFile.flush();
  }

  if (_kmlFile) {
    _kmlFile.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    _kmlFile.println("<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><name>AeroDeck V2 Track</name><Placemark><LineString><tessellate>1</tessellate><coordinates>");
    _kmlFile.flush();
  }

  _bus->releaseAllCs();
  _logPointsCount = 0;
  _loggingActive = (_csvFile || _kmlFile);
  return _loggingActive;
}

void GpsModule::closeGeoLogs() {
  if (!_loggingActive && !_csvFile && !_kmlFile) return;

  if (_bus) {
    SpiGuard lock(*_bus);
    if (lock.acquired()) {
      _bus->selectOnlyCs(PIN_SD_CS);
      if (_kmlFile) {
        _kmlFile.println("</coordinates></LineString></Placemark></Document></kml>");
        _kmlFile.flush();
        _kmlFile.close();
      }
      if (_csvFile) {
        _csvFile.flush();
        _csvFile.close();
      }
      _bus->releaseAllCs();
    } else {
      if (_kmlFile) _kmlFile.close();
      if (_csvFile) _csvFile.close();
    }
  }
  _loggingActive = false;
}

void GpsModule::writeGeoPointIfReady() {
  if (!_loggingActive || !_bus || !hasFix()) return;
  uint32_t now = millis();
  if (now - _lastLogMs < 200) return; // Matches 5 Hz max cadence
  _lastLogMs = now;

  double lat = _gps.location.lat();
  double lon = _gps.location.lng();
  double alt = _gps.altitude.isValid() ? _gps.altitude.meters() : 0.0;
  float spd = speedKmh();
  uint8_t sats = satellites();

  SpiGuard lock(*_bus);
  if (!lock.acquired()) return;

  _bus->selectOnlyCs(PIN_SD_CS);
  if (_csvFile) {
    _csvFile.printf("%.6f,%.6f,%.1f,%.1f,%u,%lu\n",
                    lat, lon, alt, spd, static_cast<unsigned>(sats), static_cast<unsigned long>(now));
  }
  if (_kmlFile) {
    _kmlFile.printf("%.6f,%.6f,%.1f\n", lon, lat, alt);
  }
  _logPointsCount++;
  if ((_logPointsCount % 10) == 0) {
    if (_csvFile) _csvFile.flush();
    if (_kmlFile) _kmlFile.flush();
  }
  _bus->releaseAllCs();
}

void GpsModule::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);
    return;
  }

  if (_view == VIEW_SPEEDO) {
    if (ev == BTN_EVT_SELECT) {
      _tripMeters = 0.0;
      _maxSpeedKmh = 0.0f;
      _hasPrevCoord = false;
    } else if (ev == BTN_EVT_UP) {
      configure5HzRate();
      if (_gui) _gui->drawToast("GPS", "5Hz UBX sent", TFT_GREEN, 500);
    }
  } else if (_view == VIEW_FIX_STATS) {
    if (ev == BTN_EVT_SELECT) {
      configurePowerSave(!_powerSaveMode);
      if (_gui) {
        _gui->drawToast("GPS PWR", _powerSaveMode ? "PowerSave ON" : "Continuous", TFT_GREEN, 600);
      }
    }
  } else if (_view == VIEW_GEO_LOG) {
    if (ev == BTN_EVT_SELECT) {
      if (_loggingActive) {
        closeGeoLogs();
      } else {
        if (!openGeoLogs() && _gui) {
          _gui->drawToast("SD ERR", "Log open failed", TFT_RED, 800);
        }
      }
    }
  }
}

void GpsModule::tick() {
  ensureUartStarted();
  bool gotNew = false;
  while (_gpsSerial.available() > 0) {
    char c = static_cast<char>(_gpsSerial.read());
    if (_gps.encode(c)) {
      gotNew = true;
    }
  }
  if (gotNew) {
    updateTripMetrics();
    writeGeoPointIfReady();
  }
}

void GpsModule::drawHeaderStrip(const char* title, const char* rightHint) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_GREEN, 0x1082);
  _gui->strip().drawString(title ? title : "GPS 5HZ", 4, 8);
  if (rightHint && rightHint[0] != '\0') {
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
    _gui->strip().drawString(rightHint, TFT_W - 4, 8);
  }
  _gui->pushStrip(0, BODY_Y_START);
}

void GpsModule::drawSpeedoView() {
  char rHint[16];
  snprintf(rHint, sizeof(rHint), "S:%u R:Tab", static_cast<unsigned>(satellites()));
  drawHeaderStrip("SPEEDO 5HZ", rHint);

  float spd = speedKmh();
  float ratio = spd / 120.0f; // Scale bar up to 120 km/h

  if (_gui->beginStrip(36, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font2);
    _gui->strip().setTextDatum(MC_DATUM);
    _gui->strip().setTextColor(hasFix() ? TFT_WHITE : TFT_YELLOW, TFT_BLACK);
    char sBuf[18];
    snprintf(sBuf, sizeof(sBuf), "%.1f km/h", spd);
    _gui->strip().drawString(sBuf, TFT_W / 2, 14);
    _gui->pushStrip(0, 36);
  }

  if (_gui->beginStrip(70, 24, TFT_BLACK)) {
    _gui->drawProgressBar(8, 5, TFT_W - 16, 12, ratio, TFT_GREEN, TFT_DARKGREY);
    _gui->pushStrip(0, 70);
  }

  if (_gui->beginStrip(98, 36, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char b1[32];
    snprintf(b1, sizeof(b1), "Max: %.1f km/h", _maxSpeedKmh);
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(b1, 6, 8);

    char b2[32];
    if (_tripMeters >= 1000.0) {
      snprintf(b2, sizeof(b2), "Trip: %.2f km", _tripMeters / 1000.0);
    } else {
      snprintf(b2, sizeof(b2), "Trip: %.0f m", _tripMeters);
    }
    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString(b2, 6, 22);
    _gui->pushStrip(0, 98);
  }
}

void GpsModule::drawFixStatsView() {
  drawHeaderStrip("GPS FIX & PWR", "R:Tab");

  if (_gui->beginStrip(38, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(hasFix() ? TFT_GREEN : TFT_RED, TFT_BLACK);
    _gui->strip().drawString(hasFix() ? "Status: 3D/2D FIX" : "Status: SEARCHING", 4, 8);

    char sBuf[32];
    snprintf(sBuf, sizeof(sBuf), "Sats: %u  Pwr: %s",
             static_cast<unsigned>(satellites()),
             _powerSaveMode ? "PSM" : "CONT");
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(sBuf, 4, 22);
    _gui->pushStrip(0, 38);
  }

  if (_gui->beginStrip(74, 36, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char latBuf[32];
    char lonBuf[32];
    if (hasFix()) {
      snprintf(latBuf, sizeof(latBuf), "Lat: %.5f", _gps.location.lat());
      snprintf(lonBuf, sizeof(lonBuf), "Lon: %.5f", _gps.location.lng());
    } else {
      snprintf(latBuf, sizeof(latBuf), "Lat: --.-----");
      snprintf(lonBuf, sizeof(lonBuf), "Lon: ---.-----");
    }
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(latBuf, 4, 8);
    _gui->strip().drawString(lonBuf, 4, 22);
    _gui->pushStrip(0, 74);
  }
}

void GpsModule::drawGeoLogView() {
  drawHeaderStrip("CSV + KML LOG", "R:Tab");

  if (_gui->beginStrip(40, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(_loggingActive ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_loggingActive ? "LOG: ON (SEL:Stop)" : "LOG: OFF (SEL:Start)", 4, 14);
    _gui->pushStrip(0, 40);
  }

  if (_gui->beginStrip(76, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char buf[32];
    snprintf(buf, sizeof(buf), "Points logged: %lu", static_cast<unsigned long>(_logPointsCount));
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(buf, 4, 14);
    _gui->pushStrip(0, 76);
  }
}

void GpsModule::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_SPEEDO:    drawSpeedoView(); break;
    case VIEW_FIX_STATS: drawFixStatsView(); break;
    case VIEW_GEO_LOG:   drawGeoLogView(); break;
    default:             drawSpeedoView(); break;
  }
}
