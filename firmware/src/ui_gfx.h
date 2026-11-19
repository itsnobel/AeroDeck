#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "config_pins.h"
#include "bus_manager.h"

// ST7735 tab adjustment macros (override in platformio.ini build_flags if needed)
#ifndef AERODECK_TFT_OFFSET_X
#define AERODECK_TFT_OFFSET_X 0
#endif

#ifndef AERODECK_TFT_OFFSET_Y
#define AERODECK_TFT_OFFSET_Y 0
#endif

#ifndef AERODECK_TFT_INVERT
#define AERODECK_TFT_INVERT false
#endif

// Safe SPI clock for 1.8" ST7735 on ESP32-C5 shared SPI2 bus
#ifndef AERODECK_TFT_SPI_HZ
#define AERODECK_TFT_SPI_HZ 20000000
#endif

// 128x32 RGB565 strip sprite (8 KB) avoids a 40 KB full-screen framebuffer
static constexpr int16_t TFT_W = 128;
static constexpr int16_t TFT_H = 160;
static constexpr int16_t STRIP_H = 32;

static constexpr int16_t TOP_BAR_H = 18;
static constexpr int16_t BOT_BAR_H = 16;
static constexpr int16_t BODY_Y_START = TOP_BAR_H;
static constexpr int16_t BODY_Y_END = TFT_H - BOT_BAR_H; // 144

enum AppMode : uint8_t {
  MODE_MENU = 0,
  MODE_WIFI_WIDS,
  MODE_BLE_AUDIT,
  MODE_SUBGHZ_RF,
  MODE_NRF24_SPEC,
  MODE_GPS_LOG,
  MODE_IR_LAB,
  MODE_SD_RUNNER,
  MODE_SYS_INFO,
  MODE_COUNT
};

struct UiStatusState {
  uint8_t batteryPercent = 100;
  float batteryVolts = 4.20f;
  bool chargingOrHigh = false;
  bool gpsFix = false;
  uint8_t gpsSats = 0;
  AppMode activeMode = MODE_MENU;
  AppMode selectedMenuMode = MODE_WIFI_WIDS;
};

class LGFX_Aerodeck : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX_Aerodeck(void);
};

class AerodeckGui {
public:
  AerodeckGui();

  bool begin(BusManager& bus);
  void drawSplashScreen();
  void drawMainMenu(const UiStatusState& state, int16_t scrollOffset);
  void drawTopBar(const UiStatusState& state);
  void drawBottomBar(const char* leftHint, const char* rightHint, uint16_t bgColor = TFT_NAVY);
  void drawToast(const char* title, const char* message, uint16_t accentColor = TFT_CYAN, uint16_t holdMs = 1200);

  // Reusable widgets for Modules (Steps 3-8)
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float ratio, uint16_t fgColor, uint16_t bgColor = TFT_DARKGREY);
  void drawSignalMeter(int16_t x, int16_t y, int16_t w, int16_t h, int rssiDbm, uint16_t bgColor = TFT_DARKGREY);
  void drawScrollList(int16_t yStart, int16_t yEnd, const char* const* items, size_t count, size_t selectedIndex, size_t& scrollTop);

  // Direct access to panel or strip sprite inside SPI-locked sections
  LGFX_Aerodeck& display() { return _tft; }
  lgfx::LGFX_Sprite& strip() { return _strip; }
  bool beginStrip(int16_t yTop, int16_t height, uint16_t bgColor = TFT_BLACK);
  void pushStrip(int16_t x, int16_t yTop);

  static const char* modeShortName(AppMode mode);
  static uint16_t modeThemeColor(AppMode mode);

private:
  BusManager* _bus = nullptr;
  LGFX_Aerodeck _tft;
  lgfx::LGFX_Sprite _strip;
  bool _stripReady = false;

  void drawModeIcon(lgfx::LovyanGFX& dst, AppMode mode, int16_t x, int16_t y, int16_t size, uint16_t color);
};
