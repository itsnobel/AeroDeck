#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

static constexpr size_t SD_MAX_ENTRIES = 24;
static constexpr size_t SD_MAX_PROFILE_CH = 16;

struct SdFileEntry {
  char name[28];
  uint32_t sizeBytes;
  bool isDir;
};

struct DeclarativeScanProfile {
  bool valid = false;
  char name[24];
  float bandMhz = 433.92f;
  uint8_t channels[SD_MAX_PROFILE_CH];
  size_t channelCount = 0;
  uint16_t stepMs = 200;
};

class SdRunnerModule {
public:
  enum SubView : uint8_t {
    VIEW_FILE_LIST = 0,
    VIEW_JSON_RUNNER,
    VIEW_COUNT
  };

  SdRunnerModule();

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
  bool _sdMounted = false;

  SubView _view = VIEW_FILE_LIST;

  SdFileEntry _entries[SD_MAX_ENTRIES];
  size_t _entryCount = 0;
  size_t _selected = 0;
  size_t _scrollTop = 0;
  char _currentDir[32];

  DeclarativeScanProfile _profile;
  bool _runningProfile = false;
  size_t _profileStepIdx = 0;
  uint32_t _lastStepMs = 0;
  uint32_t _totalProfileCycles = 0;
  char _runnerStatus[32];

  bool mountSdLocked();
  void refreshDirLocked();
  bool loadJsonProfileLocked(const char* fullPath);
  void stepDeclarativeProfile();

  void drawHeaderStrip(const char* title, const char* rightHint);
  void drawFileListView();
  void drawJsonRunnerView();
};
