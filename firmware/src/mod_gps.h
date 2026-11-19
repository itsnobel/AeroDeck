#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <SD.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

class GpsModule {
public:
  enum SubView : uint8_t {
    VIEW_SPEEDO = 0,
    VIEW_FIX_STATS,
    VIEW_GEO_LOG,
    VIEW_COUNT
  };

  GpsModule();

  void enter(BusManager& bus, AerodeckGui& gui);
  void exit();
  bool isActive() const { return _active; }

  void handleInput(ButtonEvent ev);
  void tick();
  void render();

  bool hasFix() const;
  uint8_t satellites() const;
  double latitude() const;
  double longitude() const;
  float speedKmh() const;

private:
  BusManager* _bus = nullptr;
  AerodeckGui* _gui = nullptr;
  bool _active = false;
  bool _uartStarted = false;

  SubView _view = VIEW_SPEEDO;
  HardwareSerial _gpsSerial;
  TinyGPSPlus _gps;

  bool _powerSaveMode = false;
  bool _loggingActive = false;
  uint32_t _logPointsCount = 0;
  uint32_t _lastLogMs = 0;

  double _tripMeters = 0.0;
  float _maxSpeedKmh = 0.0f;
  bool _hasPrevCoord = false;
  double _prevLat = 0.0;
  double _prevLon = 0.0;

  File _csvFile;
  File _kmlFile;

  void ensureUartStarted();
  void sendUbxMessage(uint8_t msgClass, uint8_t msgId, const uint8_t* payload, uint16_t len);
  void configure5HzRate();
  void configurePowerSave(bool enable);

  bool openGeoLogs();
  void closeGeoLogs();
  void writeGeoPointIfReady();
  void updateTripMetrics();

  void drawHeaderStrip(const char* title, const char* rightHint);
  void drawSpeedoView();
  void drawFixStatsView();
  void drawGeoLogView();
};
