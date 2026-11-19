#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

static constexpr size_t WIFI_MAX_APS = 32;

struct ScannedApEntry {
  char ssid[33];
  uint8_t bssid[6];
  int32_t rssi;
  uint8_t channel;
  wifi_auth_mode_t auth;
  bool is5Ghz;
};

struct WidsAlertStats {
  uint32_t totalFrames = 0;
  uint32_t deauthFramesSeen = 0;
  uint32_t duplicateSsidAlerts = 0;
  char lastAlertText[36];
};

class WifiWidsModule {
public:
  enum SubView : uint8_t {
    VIEW_SCAN_LIST = 0,
    VIEW_WIDS_MON,
    VIEW_PCAP_LOG,
    VIEW_NET_DIAG,
    VIEW_COUNT
  };

  WifiWidsModule();

  void enter(BusManager& bus, AerodeckGui& gui);
  void exit();
  bool isActive() const { return _active; }

  void handleInput(ButtonEvent ev);
  void tick();
  void render();

  bool hasGpsFixForLog() const { return false; }

private:
  BusManager* _bus = nullptr;
  AerodeckGui* _gui = nullptr;
  bool _active = false;

  SubView _view = VIEW_SCAN_LIST;
  ScannedApEntry _aps[WIFI_MAX_APS];
  size_t _apCount = 0;
  size_t _apSelected = 0;
  size_t _apScroll = 0;
  bool _scanning = false;
  uint32_t _lastScanMs = 0;

  WidsAlertStats _wids;
  bool _snifferRunning = false;
  bool _pcapRecording = false;
  File _pcapFile;
  uint32_t _pcapPacketsWritten = 0;

  // Authorized local subnet diagnostic summary
  char _diagLine1[32];
  char _diagLine2[32];
  char _diagLine3[32];

  void startPassiveScan();
  void pollPassiveScan();
  void evaluateDuplicateSsids();
  void setSnifferEnabled(bool enable, bool writePcap);
  bool openPcapFile();
  void closePcapFile();
  void runLocalSubnetDiag();
  void drawHeaderStrip(const char* title);
  void drawScanListView();
  void drawWidsView();
  void drawPcapView();
  void drawNetDiagView();

  static WifiWidsModule* _instance;
  static void promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  void onPromiscuousPacket(const wifi_promiscuous_pkt_t* pkt, wifi_promiscuous_pkt_type_t type);
  static const char* authToShortStr(wifi_auth_mode_t auth);
};
