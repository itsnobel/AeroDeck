#include "mod_sd_runner.h"
#include <string.h>
#include <stdio.h>

SdRunnerModule::SdRunnerModule() {
  memset(_entries, 0, sizeof(_entries));
  snprintf(_currentDir, sizeof(_currentDir), "/aerodeck");
  snprintf(_runnerStatus, sizeof(_runnerStatus), "Select .json to load");
}

bool SdRunnerModule::mountSdLocked() {
  if (!_bus) return false;
  SpiGuard lock(*_bus);
  if (!lock.acquired()) return false;

  _bus->selectOnlyCs(PIN_SD_CS);
  bool ok = SD.begin(PIN_SD_CS, SPI, 10000000);
  if (ok && !SD.exists("/aerodeck")) {
    SD.mkdir("/aerodeck");
  }
  _bus->releaseAllCs();
  _sdMounted = ok;
  return ok;
}

void SdRunnerModule::refreshDirLocked() {
  _entryCount = 0;
  _selected = 0;
  _scrollTop = 0;

  if (!_bus || !_sdMounted) return;

  SpiGuard lock(*_bus);
  if (!lock.acquired()) return;

  _bus->selectOnlyCs(PIN_SD_CS);
  File dir = SD.open(_currentDir);
  if (dir && dir.isDirectory()) {
    while (_entryCount < SD_MAX_ENTRIES) {
      File entry = dir.openNextFile();
      if (!entry) break;

      const char* rawName = entry.name();
      const char* slash = strrchr(rawName, '/');
      const char* base = slash ? (slash + 1) : rawName;

      snprintf(_entries[_entryCount].name, sizeof(_entries[_entryCount].name), "%s", base);
      _entries[_entryCount].isDir = entry.isDirectory();
      _entries[_entryCount].sizeBytes = static_cast<uint32_t>(entry.size());
      _entryCount++;
      entry.close();
    }
    dir.close();
  }
  _bus->releaseAllCs();
}

bool SdRunnerModule::loadJsonProfileLocked(const char* fullPath) {
  if (!_bus || !_sdMounted || !fullPath) return false;

  SpiGuard lock(*_bus);
  if (!lock.acquired()) return false;

  _bus->selectOnlyCs(PIN_SD_CS);
  File f = SD.open(fullPath, FILE_READ);
  if (!f) {
    _bus->releaseAllCs();
    snprintf(_runnerStatus, sizeof(_runnerStatus), "File open failed");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  _bus->releaseAllCs();

  if (err) {
    snprintf(_runnerStatus, sizeof(_runnerStatus), "JSON parse err");
    return false;
  }

  memset(&_profile, 0, sizeof(_profile));
  const char* pName = doc["name"] | "Profile";
  snprintf(_profile.name, sizeof(_profile.name), "%.22s", pName);

  float band = doc["band_mhz"] | 433.92f;
  if (band < 300.0f) band = 300.0f;
  if (band > 950.0f) band = 950.0f;
  _profile.bandMhz = band;

  uint32_t step = doc["step_ms"] | 200U;
  if (step < 50U) step = 50U;
  if (step > 5000U) step = 5000U;
  _profile.stepMs = static_cast<uint16_t>(step);

  JsonArray chArr = doc["channels"].as<JsonArray>();
  _profile.channelCount = 0;
  if (!chArr.isNull()) {
    for (JsonVariant v : chArr) {
      if (_profile.channelCount >= SD_MAX_PROFILE_CH) break;
      int ch = v.as<int>();
      if (ch >= 0 && ch <= 125) {
        _profile.channels[_profile.channelCount++] = static_cast<uint8_t>(ch);
      }
    }
  }
  if (_profile.channelCount == 0) {
    _profile.channels[0] = 1;
    _profile.channels[1] = 6;
    _profile.channels[2] = 11;
    _profile.channelCount = 3;
  }

  _profile.valid = true;
  _profileStepIdx = 0;
  _totalProfileCycles = 0;
  snprintf(_runnerStatus, sizeof(_runnerStatus), "Loaded: %s", _profile.name);
  return true;
}

void SdRunnerModule::stepDeclarativeProfile() {
  if (!_runningProfile || !_profile.valid || _profile.channelCount == 0) return;

  uint32_t now = millis();
  if (now - _lastStepMs < _profile.stepMs) return;
  _lastStepMs = now;

  uint8_t activeCh = _profile.channels[_profileStepIdx];
  _profileStepIdx = (_profileStepIdx + 1) % _profile.channelCount;
  if (_profileStepIdx == 0) {
    _totalProfileCycles++;
  }

  snprintf(_runnerStatus, sizeof(_runnerStatus), "Run %.1fM ch%u (#%lu)",
           _profile.bandMhz,
           static_cast<unsigned>(activeCh),
           static_cast<unsigned long>(_totalProfileCycles));
}

void SdRunnerModule::enter(BusManager& bus, AerodeckGui& gui) {
  _bus = &bus;
  _gui = &gui;
  _active = true;
  _view = VIEW_FILE_LIST;
  _runningProfile = false;

  if (mountSdLocked()) {
    refreshDirLocked();
  } else {
    snprintf(_runnerStatus, sizeof(_runnerStatus), "SD mount failed");
  }
}

void SdRunnerModule::exit() {
  _runningProfile = false;
  _active = false;
}

void SdRunnerModule::handleInput(ButtonEvent ev) {
  if (ev == BTN_EVT_RIGHT) {
    _runningProfile = false;
    int nextV = (static_cast<int>(_view) + 1) % static_cast<int>(VIEW_COUNT);
    _view = static_cast<SubView>(nextV);
    if (_view == VIEW_FILE_LIST && _sdMounted) {
      refreshDirLocked();
    }
    return;
  }

  if (_view == VIEW_FILE_LIST) {
    if (!_sdMounted) {
      if (ev == BTN_EVT_SELECT && mountSdLocked()) {
        refreshDirLocked();
      }
      return;
    }

    if (ev == BTN_EVT_DOWN && _entryCount > 0) {
      _selected = (_selected + 1) % _entryCount;
    } else if (ev == BTN_EVT_UP && _entryCount > 0) {
      _selected = (_selected == 0) ? (_entryCount - 1) : (_selected - 1);
    } else if (ev == BTN_EVT_SELECT && _entryCount > 0) {
      const SdFileEntry& chosen = _entries[_selected];
      size_t nLen = strlen(chosen.name);
      if (nLen >= 5 && strcasecmp(chosen.name + nLen - 5, ".json") == 0) {
        char fullPath[68];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", _currentDir, chosen.name);
        if (loadJsonProfileLocked(fullPath)) {
          _view = VIEW_JSON_RUNNER;
          _runningProfile = true;
          _lastStepMs = millis();
        }
      } else {
        if (_gui) {
          char info[30];
          snprintf(info, sizeof(info), "%lu bytes", static_cast<unsigned long>(chosen.sizeBytes));
          _gui->drawToast(chosen.name, info, TFT_YELLOW, 900);
        }
      }
    }
  } else if (_view == VIEW_JSON_RUNNER) {
    if (ev == BTN_EVT_SELECT && _profile.valid) {
      _runningProfile = !_runningProfile;
      _lastStepMs = millis();
      snprintf(_runnerStatus, sizeof(_runnerStatus), _runningProfile ? "Profile resumed" : "Profile paused");
    }
  }
}

void SdRunnerModule::tick() {
  if (_view == VIEW_JSON_RUNNER && _runningProfile) {
    stepDeclarativeProfile();
  }
}

void SdRunnerModule::drawHeaderStrip(const char* title, const char* rightHint) {
  if (!_gui || !_gui->beginStrip(BODY_Y_START, 16, 0x1082)) return;
  _gui->strip().setFont(&fonts::Font0);
  _gui->strip().setTextDatum(ML_DATUM);
  _gui->strip().setTextColor(TFT_YELLOW, 0x1082);
  _gui->strip().drawString(title ? title : "SD/JSON", 4, 8);
  if (rightHint && rightHint[0] != '\0') {
    _gui->strip().setTextDatum(MR_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, 0x1082);
    _gui->strip().drawString(rightHint, TFT_W - 4, 8);
  }
  _gui->pushStrip(0, BODY_Y_START);
}

void SdRunnerModule::drawFileListView() {
  char rHint[16];
  snprintf(rHint, sizeof(rHint), "%s R:Tab", _sdMounted ? "SD:OK" : "NO SD");
  drawHeaderStrip("/aerodeck", rHint);

  static constexpr int16_t LIST_TOP = BODY_Y_START + 16; // 34
  static constexpr int16_t ROW_H = 18;
  static constexpr size_t VIS_ROWS = 6;

  if (!_sdMounted || _entryCount == 0) {
    if (_gui->beginStrip(LIST_TOP, STRIP_H, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);
      _gui->strip().setTextDatum(MC_DATUM);
      _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      _gui->strip().drawString(_sdMounted ? "Folder empty" : "Insert SD (SEL:Retry)", TFT_W / 2, 16);
      _gui->pushStrip(0, LIST_TOP);
    }
    return;
  }

  if (_selected < _scrollTop) {
    _scrollTop = _selected;
  } else if (_selected >= _scrollTop + VIS_ROWS) {
    _scrollTop = _selected - VIS_ROWS + 1;
  }

  int16_t y = LIST_TOP;
  while (y < BODY_Y_END) {
    int16_t h = BODY_Y_END - y;
    if (h > STRIP_H) h = STRIP_H;

    if (_gui->beginStrip(y, h, TFT_BLACK)) {
      _gui->strip().setFont(&fonts::Font0);

      for (size_t i = _scrollTop; i < _entryCount && i < _scrollTop + VIS_ROWS + 2; ++i) {
        int16_t rowTop = LIST_TOP + static_cast<int16_t>((i - _scrollTop) * ROW_H);
        int16_t rowBot = rowTop + ROW_H;
        if (rowBot <= y || rowTop >= (y + h)) continue;

        int16_t ly = rowTop - y;
        bool sel = (i == _selected);
        if (sel) {
          _gui->strip().fillRect(0, ly, TFT_W, ROW_H, 0x18E3);
        }

        char line[30];
        if (_entries[i].isDir) {
          snprintf(line, sizeof(line), "[%.22s]", _entries[i].name);
        } else {
          snprintf(line, sizeof(line), "%.17s %luB", _entries[i].name, static_cast<unsigned long>(_entries[i].sizeBytes));
        }

        _gui->strip().setTextDatum(ML_DATUM);
        _gui->strip().setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, sel ? 0x18E3 : TFT_BLACK);
        _gui->strip().drawString(line, 3, ly + ROW_H / 2);
      }

      _gui->pushStrip(0, y);
    }
    y += h;
  }
}

void SdRunnerModule::drawJsonRunnerView() {
  drawHeaderStrip("JSON RUNNER", "R:Tab");

  if (_gui->beginStrip(38, 32, TFT_BLACK)) {
    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_YELLOW, TFT_BLACK);
    _gui->strip().drawString(_runnerStatus, 4, 8);

    char pInfo[32];
    if (_profile.valid) {
      snprintf(pInfo, sizeof(pInfo), "Chs:%u Step:%ums",
               static_cast<unsigned>(_profile.channelCount),
               static_cast<unsigned>(_profile.stepMs));
    } else {
      snprintf(pInfo, sizeof(pInfo), "No JSON profile loaded");
    }
    _gui->strip().setTextColor(TFT_WHITE, TFT_BLACK);
    _gui->strip().drawString(pInfo, 4, 22);
    _gui->pushStrip(0, 38);
  }

  if (_gui->beginStrip(74, 32, TFT_BLACK)) {
    float prog = (_profile.valid && _profile.channelCount > 0)
                   ? (static_cast<float>(_profileStepIdx) / static_cast<float>(_profile.channelCount))
                   : 0.0f;
    _gui->drawProgressBar(6, 4, TFT_W - 12, 10, prog, TFT_YELLOW, TFT_DARKGREY);

    _gui->strip().setFont(&fonts::Font0);
    _gui->strip().setTextDatum(ML_DATUM);
    _gui->strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _gui->strip().drawString("SEL: Pause/Resume", 6, 23);
    _gui->pushStrip(0, 74);
  }
}

void SdRunnerModule::render() {
  if (!_active || !_gui) return;
  switch (_view) {
    case VIEW_FILE_LIST:  drawFileListView(); break;
    case VIEW_JSON_RUNNER: drawJsonRunnerView(); break;
    default:               drawFileListView(); break;
  }
}
