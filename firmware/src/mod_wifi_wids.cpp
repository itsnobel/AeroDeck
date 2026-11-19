#include "mod_wifi_wids.h"
#include <string.h>
#include <stdio.h>

// Standard PCAP global header (microsecond resolution, IEEE 802.11 = 105)
struct PcapGlobalHeader {
  uint32_t magic_number;
  uint16_t version_major;
  uint16_t version_minor;
  int32_t  thiszone;
  uint32_t sigfigs;
  uint32_t snaplen;
  uint32_t network;
} __attribute__((packed));

struct PcapPacketHeader {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
} __attribute__((packed));

WifiWidsModule* WifiWidsModule::_instance = nullptr;

WifiWidsModule::WifiWidsModule() {
  memset(_aps, 0, sizeof(_aps));
  memset(&_wids, 0, sizeof(_wids));
  snprintf(_wids.lastAlertText, sizeof(_wids.lastAlertText), "No alerts");
  snprintf(_diagLine1, sizeof(_diagLine1), "STA: Disconnected");
  snprintf(_diagLine2, sizeof(_diagLine2), "IP: 0.0.0.0");
  snprintf(_diagLine3, sizeof(_diagLine3), "GW: --");
}

void WifiWidsModule::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _instance = this;
  _active = true;
  _view = VIEW_SCAN_LIST;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  startPassiveScan();
}

void WifiWidsModule::exit() {
  setSnifferEnabled(false, false);
  closePcapFile();
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  _active = false;
  _instance = nullptr;
}

const char* WifiWidsModule::authToShortStr(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA1";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA1/2";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
    default:                        return "SEC";
  }
}

void WifiWidsModule::startPassiveScan() {
  if (_scanning) return;
  WiFi.scanDelete();
  // Asynchronous passive scan across supported 2.4 GHz & 5 GHz channels
  int16_t status = WiFi.scanNetworks(true /* async */, false /* show_hidden */, true /* passive */);
  if (status == WIFI_SCAN_RUNNING) {
    _scanning = true;
    _lastScanMs = millis();
  }
}

void WifiWidsModule::evaluateDuplicateSsids() {
  _wids.duplicateSsidAlerts = 0;
  for (size_t i = 0; i < _apCount; ++i) {
    if (_aps[i].ssid[0] == '\0') continue;
    for (size_t j = i + 1; j < _apCount; ++j) {
      if (strcmp(_aps[i].ssid, _aps[j].ssid) == 0 &&
          memcmp(_aps[i].bssid, _aps[j].bssid, 6) != 0) {
        _wids.duplicateSsidAlerts++;
        snprintf(_wids.lastAlertText, sizeof(_wids.lastAlertText), "Dup SSID: %.24s", _aps[i].ssid);
      }
    }
  }
}

void WifiWidsModule::pollPassiveScan() {
  if (!_scanning) return;
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - _lastScanMs > 12000) {
      WiFi.scanDelete();
      _scanning = false;
    }
    return;
  }

  _scanning = false;
  if (n > 0) {
    size_t count = static_cast<size_t>(n);
    if (count > WIFI_MAX_APS) count = WIFI_MAX_APS;
    _apCount = 0;
    for (size_t i = 0; i < count; ++i) {
      String s = WiFi.SSID(static_cast<int>(i));
      snprintf(_aps[_apCount].ssid, sizeof(_aps[_apCount].ssid), "%s", s.c_str());
      uint8_t* mac = WiFi.BSSID(static_cast<int>(i));
      if (mac) {
        memcpy(_aps[_apCount].bssid, mac, 6);
      } else {
        memset(_aps[_apCount].bssid, 0, 6);
      }
      _aps[_apCount].rssi = WiFi.RSSI(static_cast<int>(i));
      int32_t ch = WiFi.channel(static_cast<int>(i));
      _aps[_apCount].channel = (ch > 0 && ch < 255) ? static_cast<uint8_t>(ch) : 0;
      _aps[_apCount].auth = WiFi.encryptionType(static_cast<int>(i));
      _aps[_apCount].is5Ghz = (_aps[_apCount].channel > 14);
      _apCount++;
    }
    if (_apSelected >= _apCount) {
      _apSelected = (_apCount > 0) ? (_apCount - 1) : 0;
    }
    evaluateDuplicateSsids();
  }
  WiFi.scanDelete();
}

bool WifiWidsModule::openPcapFile() {
  if (!_bus) return false;
  closePcapFile();

  SpiGuard lock(*_bus);
  if (!lock.acquired()) return false;

  _bus->selectOnlyCs(PIN_SD_CS);
  bool sdOk = SD.begin(PIN_SD_CS, SPI, 10000000);
  if (!sdOk) {
    _bus->releaseAllCs();
    return false;
  }

  if (!SD.exists("/aerodeck")) {
    SD.mkdir("/aerodeck");
  }

  char path[40];
  snprintf(path, sizeof(path), "/aerodeck/wids_%lu.pcap", static_cast<unsigned long>(millis() / 1000));
  _pcapFile = SD.open(path, FILE_WRITE);
  if (!_pcapFile) {
    _bus->releaseAllCs();
    return false;
  }

  PcapGlobalHeader gh = {
    0xa1b2c3d4,
    2,
    4,
    0,
    0,
    65535,
    105 // LINKTYPE_IEEE802_11
  };
  _pcapFile.write(reinterpret_cast<const uint8_t*>(&gh), sizeof(gh));
  _pcapFile.flush();
  _pcapPacketsWritten = 0;
  _bus->releaseAllCs();
  return true;
}

void WifiWidsModule::closePcapFile() {
  if (!_pcapFile) return;
  if (_bus) {
    SpiGuard lock(*_bus);
    if (lock.acquired()) {
      _bus->selectOnlyCs(PIN_SD_CS);
      _pcapFile.flush();
      _pcapFile.close();
      _bus->releaseAllCs();
    } else {
      _pcapFile.close();
    }
  } else {
    _pcapFile.close();
  }
  _pcapRecording = false;
}

void WifiWidsModule::setSnifferEnabled(bool enable, bool writePcap) {
  if (enable) {
    if (writePcap && !_pcapFile) {
      if (!openPcapFile()) {
        if (_gui) _gui->drawToast("SD ERROR", "PCAP open failed", TFT_RED, 900);
        return;
      }
      _pcapRecording = true;
    }
    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&WifiWidsModule::promiscuousCb);
    esp_wifi_set_promiscuous(true);
    _snifferRunning = true;
  } else {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    _snifferRunning = false;
    closePcapFile();
  }
}

void WifiWidsModule::promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!_instance || !buf) return;
  _instance->onPromiscuousPacket(reinterpret_cast<const wifi_promiscuous_pkt_t*>(buf), type);
}

void WifiWidsModule::onPromiscuousPacket(const wifi_promiscuous_pkt_t* pkt, wifi_promiscuous_pkt_type_t type) {
  if (!pkt) return;
  _wids.totalFrames++;

  // Frame Control byte 0: bits 3..2 = type, bits 7..4 = subtype.
  // Management frame (type 0) subtype 0x0C = Deauthentication (passive detection only).
  if (type == WIFI_PKT_MGMT && pkt->rx_ctrl.sig_len >= 2) {
    uint8_t fc0 = pkt->payload[0];
    uint8_t subtype = (fc0 >> 4) & 0x0F;
    if (subtype == 0x0C) {
      _wids.deauthFramesSeen++;
      snprintf(_wids.lastAlertText, sizeof(_wids.lastAlertText), "Deauth ch%u %ddBm",
               static_cast<unsigned>(pkt->rx_ctrl.channel), static_cast<int>(pkt->rx_ctrl.rssi));
    }
  }

  if (_pcapRecording && _pcapFile && _bus) {
    uint32_t len = pkt->rx_ctrl.sig_len;
    if (len > 2500) len = 2500;
    uint32_t nowMs = millis();
    PcapPacketHeader ph = {
      nowMs / 1000,
      (nowMs % 1000) * 1000,
      len,
      len
    };

    // Non-blocking tryLock so radio ISR/task never stalls on SPI2
    if (_bus->tryLockSpi(5)) {
      _bus->selectOnlyCs(PIN_SD_CS);
      _pcapFile.write(reinterpret_cast<const uint8_t*>(&ph), sizeof(ph));
      _pcapFile.write(pkt->payload, len);
      if ((_pcapPacketsWritten++ % 25) == 0) {
        _pcapFile.flush();
      }
      _bus->releaseAllCs();
      _bus->unlockSpi();
    }
  }
}

void WifiWidsModule::runLocalSubnetDiag() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    IPAddress gw = WiFi.gatewayIP();
    int32_t rssi = WiFi.RSSI();
    snprintf(_diagLine1, sizeof(_diagLine1), "STA: OK (%ld dBm)", static_cast<long>(rssi));
    snprintf(_diagLine2, sizeof(_diagLine2), "IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    snprintf(_diagLine3, sizeof(_diagLine3), "GW: %u.%u.%u.%u", gw[0], gw[1], gw[2], gw[3]);
  } else {
    snprintf(_diagLine1, sizeof(_diagLine1), "STA: Not connected");
    snprintf(_diagLine2, sizeof(_diagLine2), "APs seen: %u", static_cast<unsigned>(_apCount));
    snprintf(_diagLine3, sizeof(_diagLine3), "Passive mode only");
  }
}

void WifiWidsModule::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    setSnifferEnabled(false, false);
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);
    if (_view == VIEW_SCAN_LIST) {
      startPassiveScan();
    } else if (_view == VIEW_WIDS_MON) {
      setSnifferEnabled(true, false);
    } else if (_view == VIEW_NET_DIAG) {
      runLocalSubnetDiag();
    }
    return;
  }

  if (_view == VIEW_SCAN_LIST) {
    if (ev == BTN_EVT_DOWN && _apCount > 0) {
      _apSelected = (_apSelected + 1) % _apCount;
    } else if (ev == BTN_EVT_UP && _apCount > 0) {
      _apSelected = (_apSelected == 0) ? (_apCount - 1) : (_apSelected - 1);
    } else if (ev == BTN_EVT_SELECT) {
      startPassiveScan();
    }
  } else if (_view == VIEW_WIDS_MON) {
    if (ev == BTN_EVT_SELECT) {
      setSnifferEnabled(!_snifferRunning, false);
    }
  } else if (_view == VIEW_PCAP_LOG) {
    if (ev == BTN_EVT_SELECT) {
      if (_pcapRecording) {
        setSnifferEnabled(false, false);
      } else {
        setSnifferEnabled(true, true);
      }
    }
  } else if (_view == VIEW_NET_DIAG) {
    if (ev == BTN_EVT_SELECT) {
      runLocalSubnetDiag();
    }
  }
}

void WifiWidsModule::tick() {
  pollPassiveScan();
}

void WifiWidsModule::drawHeaderStrip(const char* title) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_CYAN, 0x1082);
  _gui->strip().drawString(title ? title : "WIFI6", 4, 8);
  _gui->strip().setTextDatum(MR_DATUM);
  _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
  _gui->strip().drawString("R:Tab", TFT_W - 4, 8);
  _gui->pushStrip(0, BODY_Y_START);
}

void WifiWidsModule::drawScanListView() {
  drawHeaderStrip(_scanning ? "SCANNING..." : "AP SCAN (2.4/5G)");

  static constexpr int16_t LIST_TOP = BODY_Y_START + 16; // 34
  static constexpr int16_t ROW_H = 22;
  static constexpr size_t VIS_ROWS = 5; // 34..144 = 110px

  if (_apCount == 0) {
    if (_gui->beginStrip(LIST_TOP, STRIP_H, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);
      _gui->strip().setTextDatum(MC_DATUM);
      _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      _gui->strip().drawString(_scanning ? "Scanning channels..." : "No APs yet (SEL:Scan)", TFT_W / 2, 16);
      _gui->pushStrip(0, LIST_TOP);
    }
    return;
  }

  if (_apSelected < _apScroll) {
    _apScroll = _apSelected;
  } else if (_apSelected >= _apScroll + VIS_ROWS) {
    _apScroll = _apSelected - VIS_ROWS + 1;
  }

  int16_t y = LIST_TOP;
  while (y < BODY_Y_END) {
    int16_t h = BODY_Y_END - y;
    if (h > STRIP_H) h = STRIP_H;

    if (_gui->beginStrip(y, h, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);

      for (size_t i = _apScroll; i < _apCount && i < _apScroll + VIS_ROWS + 2; ++i) {
        int16_t rowTop = LIST_TOP + static_cast<int16_t>((i - _apScroll) * ROW_H);
        int16_t rowBot = rowTop + ROW_H;
        if (rowBot <= y || rowTop >= (y + h)) continue;

        int16_t ly = rowTop - y;
        bool sel = (i == _apSelected);
        if (sel) {
          _gui->strip().fillRect(0, ly, TFT_W, ROW_H, 0x18E3);
        }

        const char* name = (_aps[i].ssid[0] != '\0') ? _aps[i].ssid : "<HIDDEN>";
        char line1[28];
        snprintf(line1, sizeof(line1), "%.16s %s", name, _aps[i].is5Ghz ? "5G" : "2.4G");

        char line2[28];
        snprintf(line2, sizeof(line2), "C%u %s %lddBm",
                 static_cast<unsigned>(_aps[i].channel),
                 authToShortStr(_aps[i].auth),
                 static_cast<long>(_aps[i].rssi));

        _gui->strip().setTextDatum(ML_DATUM);
        _gui->strip().setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, sel ? 0x18E3 : TFT_BLACK);
        _gui->strip().drawString(line1, 3, ly + 6);

        _gui->strip().setTextColor(sel ? TFT_CYAN : TFT_DARKGREY, sel ? 0x18E3 : TFT_BLACK);
        _gui->strip().drawString(line2, 3, ly + 16);
      }

      _gui->pushStrip(0, y);
    }
    y += h;
  }
}

void WifiWidsModule::drawWidsView() {
  drawHeaderStrip("DEFENSIVE WIDS");

  if (_gui->beginStrip(36, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char b1[32];
    snprintf(b1, sizeof(b1), "State: %s", _snifferRunning ? "MONITORING" : "PAUSED");
    _gui->strip().setTextColor(_snifferRunning ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(b1, 4, 8);

    char b2[32];
    snprintf(b2, sizeof(b2), "Deauth alerts: %lu", static_cast<unsigned long>(_wids.deauthFramesSeen));
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(b2, 4, 22);
    _gui->pushStrip(0, 36);
  }

  if (_gui->beginStrip(70, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char b3[32];
    snprintf(b3, sizeof(b3), "Dup SSIDs: %lu", static_cast<unsigned long>(_wids.duplicateSsidAlerts));
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(b3, 4, 8);

    char b4[32];
    snprintf(b4, sizeof(b4), "Frames: %lu", static_cast<unsigned long>(_wids.totalFrames));
    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString(b4, 4, 22);
    _gui->pushStrip(0, 70);
  }

  if (_gui->beginStrip(104, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_ORANGE, TFT_BLACK);
    _gui->strip().drawString(_wids.lastAlertText, 4, 14);
    _gui->pushStrip(0, 104);
  }
}

void WifiWidsModule::drawPcapView() {
  drawHeaderStrip("SD PCAP LOGGER");

  if (_gui->beginStrip(40, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(_pcapRecording ? TFT_RED : TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(_pcapRecording ? "REC: ON (SEL:Stop)" : "REC: OFF (SEL:Start)", 4, 14);
    _gui->pushStrip(0, 40);
  }

  if (_gui->beginStrip(76, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    char buf[32];
    snprintf(buf, sizeof(buf), "Packets: %lu", static_cast<unsigned long>(_pcapPacketsWritten));
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(buf, 4, 14);
    _gui->pushStrip(0, 76);
  }
}

void WifiWidsModule::drawNetDiagView() {
  drawHeaderStrip("NET DIAG");

  if (_gui->beginStrip(40, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(_diagLine1, 4, 14);
    _gui->pushStrip(0, 40);
  }

  if (_gui->beginStrip(74, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_CYAN, TFT_BLACK);
    _gui->strip().drawString(_diagLine2, 4, 14);
    _gui->pushStrip(0, 74);
  }

  if (_gui->beginStrip(108, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString(_diagLine3, 4, 14);
    _gui->pushStrip(0, 108);
  }
}

void WifiWidsModule::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_SCAN_LIST: drawScanListView(); break;
    case VIEW_WIDS_MON:    drawWidsView(); break;
    case VIEW_PCAP_LOG:    drawPcapView(); break;
    case VIEW_NET_DIAG:    drawNetDiagView(); break;
    default:               drawScanListView(); break;
  }
}
