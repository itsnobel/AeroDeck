#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

static constexpr size_t BLE_MAX_DEVICES = 24;
static constexpr size_t BLE_MAX_GATT_ITEMS = 20;

struct BleAdvEntry {
  char name[25];
  char addr[18];
  int rssi;
  bool connectable;
  bool hasServiceUuid;
  char serviceUuid[12];
};

struct GattEntry {
  char label[30];
  bool isService;
};

class BleAuditModule {
public:
  enum SubView : uint8_t {
    VIEW_ADV_LIST = 0,
    VIEW_GATT_INSPECT,
    VIEW_COUNT
  };

  BleAuditModule();

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
  bool _stackInitialized = false;

  SubView _view = VIEW_ADV_LIST;

  BleAdvEntry _devices[BLE_MAX_DEVICES];
  size_t _devCount = 0;
  size_t _devSelected = 0;
  size_t _devScroll = 0;

  bool _scanning = false;
  uint32_t _lastScanStartMs = 0;

  // Read-only GATT inspector state
  GattEntry _gattItems[BLE_MAX_GATT_ITEMS];
  size_t _gattCount = 0;
  size_t _gattSelected = 0;
  size_t _gattScroll = 0;
  bool _gattBusy = false;
  char _gattStatus[32];

  void ensureBleStack();
  void teardownBleStack();
  void startAdvScan(uint8_t durationSec = 2);
  void pollAdvScan();
  void inspectSelectedGatt();

  void drawHeaderStrip(const char* title, const char* rightHint);
  void drawAdvListView();
  void drawGattInspectView();

  friend class AeroDeckBleScanCallbacks;
};
