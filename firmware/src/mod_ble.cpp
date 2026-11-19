#include "mod_ble.h"
#include <string.h>
#include <stdio.h>

class AeroDeckBleScanCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  explicit AeroDeckBleScanCallbacks(BleAuditModule* owner) : _owner(owner) {}

  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!_owner || _owner->_devCount >= BLE_MAX_DEVICES) return;

    std::string addrStr = advertisedDevice.getAddress().toString();
    for (size_t i = 0; i < _owner->_devCount; ++i) {
      if (strcmp(_owner->_devices[i].addr, addrStr.c_str()) == 0) {
        _owner->_devices[i].rssi = advertisedDevice.getRSSI();
        if (advertisedDevice.haveName()) {
          snprintf(_owner->_devices[i].name, sizeof(_owner->_devices[i].name),
                   "%s", advertisedDevice.getName().c_str());
        }
        return;
      }
    }

    BleAdvEntry& entry = _owner->_devices[_owner->_devCount];
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.addr, sizeof(entry.addr), "%s", addrStr.c_str());

    if (advertisedDevice.haveName()) {
      snprintf(entry.name, sizeof(entry.name), "%s", advertisedDevice.getName().c_str());
    } else {
      snprintf(entry.name, sizeof(entry.name), "<UNNAMED>");
    }

    entry.rssi = advertisedDevice.getRSSI();
    entry.connectable = advertisedDevice.isConnectable();

    if (advertisedDevice.haveServiceUUID()) {
      entry.hasServiceUuid = true;
      snprintf(entry.serviceUuid, sizeof(entry.serviceUuid), "%s",
               advertisedDevice.getServiceUUID().toString().c_str());
    } else {
      entry.hasServiceUuid = false;
      snprintf(entry.serviceUuid, sizeof(entry.serviceUuid), "-");
    }

    _owner->_devCount++;
  }

private:
  BleAuditModule* _owner;
};

static AeroDeckBleScanCallbacks* s_scanCb = nullptr;

BleAuditModule::BleAuditModule() {
  memset(_devices, 0, sizeof(_devices));
  memset(_gattItems, 0, sizeof(_gattItems));
  snprintf(_gattStatus, sizeof(_gattStatus), "SELECT to read GATT");
}

void BleAuditModule::ensureBleStack() {
  if (_stackInitialized) return;
  BLEDevice::init("AeroDeckV2");
  BLEScan* pScan = BLEDevice::getScan();
  if (pScan) {
    if (!s_scanCb) {
      s_scanCb = new AeroDeckBleScanCallbacks(this);
    }
    pScan->setAdvertisedDeviceCallbacks(s_scanCb, true /* wantDuplicates */);
    pScan->setActiveScan(false); // Strictly passive advertisement reception
    pScan->setInterval(100);
    pScan->setWindow(80);
  }
  _stackInitialized = true;
}

void BleAuditModule::teardownBleStack() {
  if (!_stackInitialized) return;
  BLEScan* pScan = BLEDevice::getScan();
  if (pScan) {
    pScan->stop();
    pScan->clearResults();
  }
  // Release BLE controller & NimBLE/Bluedroid resources cleanly on mode exit
  BLEDevice::deinit(true);
  _stackInitialized = false;
  _scanning = false;
}

void BleAuditModule::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _active = true;
  _view = VIEW_ADV_LIST;
  _gattBusy = false;

  ensureBleStack();
  startAdvScan(2);
}

void BleAuditModule::exit() {
  teardownBleStack();
  _active = false;
}

void BleAuditModule::startAdvScan(uint8_t durationSec) {
  ensureBleStack();
  if (_scanning) return;

  BLEScan* pScan = BLEDevice::getScan();
  if (!pScan) return;

  _scanning = true;
  _lastScanStartMs = millis();
  // Non-blocking scan start with callback
  pScan->start(durationSec, nullptr, false);
}

void BleAuditModule::pollAdvScan() {
  if (!_scanning || !_stackInitialized) return;

  BLEScan* pScan = BLEDevice::getScan();
  if (!pScan) {
    _scanning = false;
    return;
  }

  // Fallback timeout guard in case async flag stays set
  if (millis() - _lastScanStartMs > 6000) {
    pScan->stop();
    pScan->clearResults();
    _scanning = false;
  } else if (!pScan->isScanning()) {
    pScan->clearResults();
    _scanning = false;
  }
}

void BleAuditModule::inspectSelectedGatt() {
  if (_gattBusy || _devCount == 0 || _devSelected >= _devCount) return;

  const BleAdvEntry& target = _devices[_devSelected];
  _gattBusy = true;
  _gattCount = 0;
  _gattSelected = 0;
  _gattScroll = 0;

  if (!target.connectable) {
    snprintf(_gattStatus, sizeof(_gattStatus), "Non-connectable ADV");
    _gattBusy = false;
    return;
  }

  if (_gui) {
    _gui->drawToast("BLE GATT", "Connecting...", TFT_SKYBLUE, 300);
  }

  ensureBleStack();
  BLEClient* pClient = BLEDevice::createClient();
  if (!pClient) {
    snprintf(_gattStatus, sizeof(_gattStatus), "Client alloc failed");
    _gattBusy = false;
    return;
  }

  BLEAddress bleAddr(target.addr);
  if (!pClient->connect(bleAddr)) {
    snprintf(_gattStatus, sizeof(_gattStatus), "Connect failed");
    delete pClient;
    _gattBusy = false;
    return;
  }

  std::map<std::string, BLERemoteService*>* services = pClient->getServices();
  if (services && !services->empty()) {
    for (auto& kv : *services) {
      if (_gattCount >= BLE_MAX_GATT_ITEMS) break;

      BLERemoteService* pSvc = kv.second;
      if (!pSvc) continue;

      std::string sUuid = pSvc->getUUID().toString();
      snprintf(_gattItems[_gattCount].label, sizeof(_gattItems[_gattCount].label),
               "S: %.25s", sUuid.c_str());
      _gattItems[_gattCount].isService = true;
      _gattCount++;

      std::map<std::string, BLERemoteCharacteristic*>* chars = pSvc->getCharacteristics();
      if (chars) {
        for (auto& ckv : *chars) {
          if (_gattCount >= BLE_MAX_GATT_ITEMS) break;
          BLERemoteCharacteristic* pChr = ckv.second;
          if (!pChr) continue;

          char props[6] = {0};
          size_t pi = 0;
          if (pChr->canRead() && pi < 5) props[pi++] = 'R';
          if (pChr->canWrite() && pi < 5) props[pi++] = 'W';
          if (pChr->canNotify() && pi < 5) props[pi++] = 'N';
          if (pi == 0) props[0] = '-';

          std::string cUuid = pChr->getUUID().toString();
          snprintf(_gattItems[_gattCount].label, sizeof(_gattItems[_gattCount].label),
                   " C[%s]:%.18s", props, cUuid.c_str());
          _gattItems[_gattCount].isService = false;
          _gattCount++;
        }
      }
    }
    snprintf(_gattStatus, sizeof(_gattStatus), "Found %u entries", static_cast<unsigned>(_gattCount));
  } else {
    snprintf(_gattStatus, sizeof(_gattStatus), "No GATT services");
  }

  pClient->disconnect();
  delete pClient;
  _gattBusy = false;
}

void BleAuditModule::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);
    if (_view == VIEW_GATT_INSPECT && _gattCount == 0 && _devCount > 0) {
      snprintf(_gattStatus, sizeof(_gattStatus), "SEL: Read %.14s", _devices[_devSelected].name);
    }
    return;
  }

  if (_view == VIEW_ADV_LIST) {
    if (ev == BTN_EVT_DOWN && _devCount > 0) {
      _devSelected = (_devSelected + 1) % _devCount;
    } else if (ev == BTN_EVT_UP && _devCount > 0) {
      _devSelected = (_devSelected == 0) ? (_devCount - 1) : (_devSelected - 1);
    } else if (ev == BTN_EVT_SELECT) {
      startAdvScan(2);
    }
  } else if (_view == VIEW_GATT_INSPECT) {
    if (ev == BTN_EVT_DOWN && _gattCount > 0) {
      _gattSelected = (_gattSelected + 1) % _gattCount;
    } else if (ev == BTN_EVT_UP && _gattCount > 0) {
      _gattSelected = (_gattSelected == 0) ? (_gattCount - 1) : (_gattSelected - 1);
    } else if (ev == BTN_EVT_SELECT) {
      inspectSelectedGatt();
    }
  }
}

void BleAuditModule::tick() {
  pollAdvScan();
}

void BleAuditModule::drawHeaderStrip(const char* title, const char* rightHint) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_SKYBLUE, 0x1082);
  _gui->strip().drawString(title ? title : "BLE", 4, 8);

  if (rightHint && rightHint[0] != '\0') {
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
    _gui->strip().drawString(rightHint, TFT_W - 4, 8);
  }
  _gui->pushStrip(0, BODY_Y_START);
}

void BleAuditModule::drawAdvListView() {
  char hdrRight[16];
  snprintf(hdrRight, sizeof(hdrRight), _scanning ? "SCAN..." : "N:%u", static_cast<unsigned>(_devCount));
  drawHeaderStrip("BLE ADV SCAN", hdrRight);

  static constexpr int16_t LIST_TOP = BODY_Y_START + 16; // 34
  static constexpr int16_t ROW_H = 22;
  static constexpr size_t VIS_ROWS = 5;

  if (_devCount == 0) {
    if (_gui->beginStrip(LIST_TOP, STRIP_H, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);
      _gui->strip().setTextDatum(MC_DATUM);
      _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      _gui->strip().drawString(_scanning ? "Listening ADV..." : "No BLE yet (SEL:Scan)", TFT_W / 2, 16);
      _gui->pushStrip(0, LIST_TOP);
    }
    return;
  }

  if (_devSelected < _devScroll) {
    _devScroll = _devSelected;
  } else if (_devSelected >= _devScroll + VIS_ROWS) {
    _devScroll = _devSelected - VIS_ROWS + 1;
  }

  int16_t y = LIST_TOP;
  while (y < BODY_Y_END) {
    int16_t h = BODY_Y_END - y;
    if (h > STRIP_H) h = STRIP_H;

    if (_gui->beginStrip(y, h, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);

      for (size_t i = _devScroll; i < _devCount && i < _devScroll + VIS_ROWS + 2; ++i) {
        int16_t rowTop = LIST_TOP + static_cast<int16_t>((i - _devScroll) * ROW_H);
        int16_t rowBot = rowTop + ROW_H;
        if (rowBot <= y || rowTop >= (y + h)) continue;

        int16_t ly = rowTop - y;
        bool sel = (i == _devSelected);
        if (sel) {
          _gui->strip().fillRect(0, ly, TFT_W, ROW_H, 0x18E3);
        }

        char line1[30];
        snprintf(line1, sizeof(line1), "%.16s %ddBm", _devices[i].name, _devices[i].rssi);

        char line2[30];
        snprintf(line2, sizeof(line2), "%.17s %s", _devices[i].addr, _devices[i].connectable ? "CONN" : "ADV");

        _gui->strip().setTextDatum(ML_DATUM);
        _gui->strip().setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, sel ? 0x18E3 : TFT_BLACK);
        _gui->strip().drawString(line1, 3, ly + 6);

        _gui->strip().setTextColor(sel ? TFT_SKYBLUE : TFT_DARKGREY, sel ? 0x18E3 : TFT_BLACK);
        _gui->strip().drawString(line2, 3, ly + 16);
      }

      _gui->pushStrip(0, y);
    }
    y += h;
  }
}

void BleAuditModule::drawGattInspectView() {
  drawHeaderStrip("GATT INSPECT", "R:Tab");

  if (_gui->beginStrip(34, 16, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_gattStatus, 4, 8);
    _gui->pushStrip(0, 34);
  }

  static constexpr int16_t LIST_TOP = 50;
  static constexpr int16_t ROW_H = 15;
  static constexpr size_t VIS_ROWS = 6;

  if (_gattCount == 0) {
    if (_gui->beginStrip(LIST_TOP, STRIP_H, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);
      _gui->strip().setTextDatum(MC_DATUM);
      _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      _gui->strip().drawString("Press SEL to inspect", TFT_W / 2, 16);
      _gui->pushStrip(0, LIST_TOP);
    }
    return;
  }

  if (_gattSelected < _gattScroll) {
    _gattScroll = _gattSelected;
  } else if (_gattSelected >= _gattScroll + VIS_ROWS) {
    _gattScroll = _gattSelected - VIS_ROWS + 1;
  }

  int16_t y = LIST_TOP;
  while (y < BODY_Y_END) {
    int16_t h = BODY_Y_END - y;
    if (h > STRIP_H) h = STRIP_H;

    if (_gui->beginStrip(y, h, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);
      _gui->strip().setTextDatum(ML_DATUM);

      for (size_t i = _gattScroll; i < _gattCount && i < _gattScroll + VIS_ROWS + 2; ++i) {
        int16_t rowTop = LIST_TOP + static_cast<int16_t>((i - _gattScroll) * ROW_H);
        int16_t rowBot = rowTop + ROW_H;
        if (rowBot <= y || rowTop >= (y + h)) continue;

        int16_t ly = rowTop - y;
        bool sel = (i == _gattSelected);
        uint16_t fg = _gattItems[i].isService ? TFT_CYAN : TFT_LIGHTGREY;
        if (sel) {
          _gui->strip().fillRect(0, ly, TFT_W, ROW_H, 0x18E3);
          fg = TFT_WHITE;
        }
        _gui->strip().setTextColor(fg, sel ? 0x18E3 : TFT_BLACK);
        _gui->strip().drawString(_gattItems[i].label, 3, ly + ROW_H / 2);
      }

      _gui->pushStrip(0, y);
    }
    y += h;
  }
}

void BleAuditModule::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_ADV_LIST:    drawAdvListView(); break;
    case VIEW_GATT_INSPECT: drawGattInspectView(); break;
    default:               drawAdvListView(); break;
  }
}
