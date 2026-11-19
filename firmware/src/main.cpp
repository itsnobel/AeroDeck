#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"
#include "mod_wifi_wids.h"
#include "mod_ble.h"
#include "mod_subghz.h"
#include "mod_nrf24.h"
#include "mod_gps.h"
#include "mod_ir.h"
#include "mod_sd_runner.h"

static BusManager bus;
static AerodeckGui gui;
static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

static WifiWidsModule modWifi;
static BleAuditModule modBle;
static SubGhzModule modSubGhz;
static Nrf24Module modNrf24;
static GpsModule modGps;
static IrLabModule modIr;
static SdRunnerModule modSd;

static UiStatusState uiState;
static int16_t menuScroll = 0;
static uint32_t lastTelemetryMs = 0;
static uint32_t lastRenderMs = 0;

static void applyNeoPixelForMode(AppMode mode) {
  uint32_t col = pixel.Color(0, 16, 20);
  switch (mode) {
    case MODE_MENU:       col = pixel.Color(0, 16, 20); break;
    case MODE_WIFI_WIDS:  col = pixel.Color(0, 24, 24); break;
    case MODE_BLE_AUDIT:  col = pixel.Color(0, 12, 28); break;
    case MODE_SUBGHZ_RF:  col = pixel.Color(28, 12, 0); break;
    case MODE_NRF24_SPEC: col = pixel.Color(24, 0, 24); break;
    case MODE_GPS_LOG:    col = pixel.Color(0, 24, 6);  break;
    case MODE_IR_LAB:     col = pixel.Color(28, 0, 4);  break;
    case MODE_SD_RUNNER:  col = pixel.Color(24, 20, 0); break;
    case MODE_SYS_INFO:   col = pixel.Color(18, 18, 18); break;
    default:              col = pixel.Color(0, 16, 20); break;
  }
  pixel.setPixelColor(0, col);
  pixel.show();
}

static void exitMode(AppMode mode) {
  switch (mode) {
    case MODE_WIFI_WIDS:  modWifi.exit(); break;
    case MODE_BLE_AUDIT:  modBle.exit(); break;
    case MODE_SUBGHZ_RF:  modSubGhz.exit(); break;
    case MODE_NRF24_SPEC: modNrf24.exit(); break;
    case MODE_GPS_LOG:    modGps.exit(); break;
    case MODE_IR_LAB:     modIr.exit(); break;
    case MODE_SD_RUNNER:  modSd.exit(); break;
    default:              break;
  }
}

static void enterMode(AppMode mode) {
  // Always clean up previous active mode before starting new radio/bus activity
  if (uiState.activeMode != MODE_MENU && uiState.activeMode != mode) {
    exitMode(uiState.activeMode);
  }

  uiState.activeMode = mode;
  applyNeoPixelForMode(mode);

  switch (mode) {
    case MODE_WIFI_WIDS:  modWifi.enter(bus, gui); break;
    case MODE_BLE_AUDIT:  modBle.enter(bus, gui); break;
    case MODE_SUBGHZ_RF:  modSubGhz.enter(bus, gui); break;
    case MODE_NRF24_SPEC: modNrf24.enter(bus, gui); break;
    case MODE_GPS_LOG:    modGps.enter(bus, gui); break;
    case MODE_IR_LAB:     modIr.enter(bus, gui); break;
    case MODE_SD_RUNNER:  modSd.enter(bus, gui); break;
    case MODE_MENU:
    case MODE_SYS_INFO:
    default:              break;
  }
}

static void refreshTelemetry() {
  uiState.batteryVolts = bus.readBatteryVolts();
  uiState.batteryPercent = bus.readBatteryPercent();
  uiState.gpsFix = modGps.hasFix();
  uiState.gpsSats = modGps.satellites();
}

static void renderSysInfoScreen() {
  gui.drawTopBar(uiState);

  if (gui.beginStrip(34, 32, TFT_BLACK)) {
    gui.strip().setFont(&fonts::Font0);
    gui.strip().setTextDatum(ML_DATUM);
    char b1[32];
    snprintf(b1, sizeof(b1), "Bat: %.2fV (%u%%)", uiState.batteryVolts, static_cast<unsigned>(uiState.batteryPercent));
    gui.strip().setTextColor(TFT_GREEN, TFT_BLACK);
    gui.strip().drawString(b1, 4, 8);

    char b2[32];
    uint32_t freeHeap = esp_get_free_heap_size() / 1024U;
    snprintf(b2, sizeof(b2), "Free SRAM: %lu KB", static_cast<unsigned long>(freeHeap));
    gui.strip().setTextColor(TFT_CYAN, TFT_BLACK);
    gui.strip().drawString(b2, 4, 22);
    gui.pushStrip(0, 34);
  }

  if (gui.beginStrip(70, 32, TFT_BLACK)) {
    gui.strip().setFont(&fonts::Font0);
    gui.strip().setTextDatum(ML_DATUM);
    gui.strip().setTextColor(TFT_WHITE, TFT_BLACK);
    gui.strip().drawString("MCU: ESP32-C5 RISC-V", 4, 8);
    gui.strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    gui.strip().drawString("SPI2 Mutex + 128x32 UI", 4, 22);
    gui.pushStrip(0, 70);
  }

  gui.drawBottomBar("LEFT/SEL:Menu", "", TFT_NAVY);
}

static void renderCurrentMode() {
  if (uiState.activeMode == MODE_MENU) {
    gui.drawMainMenu(uiState, menuScroll);
    return;
  }

  if (uiState.activeMode == MODE_SYS_INFO) {
    renderSysInfoScreen();
    return;
  }

  gui.drawTopBar(uiState);

  switch (uiState.activeMode) {
    case MODE_WIFI_WIDS:  modWifi.render(); break;
    case MODE_BLE_AUDIT:  modBle.render(); break;
    case MODE_SUBGHZ_RF:  modSubGhz.render(); break;
    case MODE_NRF24_SPEC: modNrf24.render(); break;
    case MODE_GPS_LOG:    modGps.render(); break;
    case MODE_IR_LAB:     modIr.render(); break;
    case MODE_SD_RUNNER:  modSd.render(); break;
    default:              break;
  }

  gui.drawBottomBar("L/SEL:Back", "R:SubView", TFT_NAVY);
}

static void tickActiveModule() {
  switch (uiState.activeMode) {
    case MODE_WIFI_WIDS:  modWifi.tick(); break;
    case MODE_BLE_AUDIT:  modBle.tick(); break;
    case MODE_SUBGHZ_RF:  modSubGhz.tick(); break;
    case MODE_NRF24_SPEC: modNrf24.tick(); break;
    case MODE_GPS_LOG:    modGps.tick(); break;
    case MODE_IR_LAB:     modIr.tick(); break;
    case MODE_SD_RUNNER:  modSd.tick(); break;
    default:              break;
  }
}

static void dispatchModeInput(ButtonEvent ev) {
  if (uiState.activeMode == MODE_MENU) {
    const int firstMode = static_cast<int>(MODE_WIFI_WIDS);
    const int lastMode = static_cast<int>(MODE_COUNT) - 1;
    int sel = static_cast<int>(uiState.selectedMenuMode);

    if (ev == BTN_EVT_DOWN) {
      sel = (sel >= lastMode) ? firstMode : (sel + 1);
      uiState.selectedMenuMode = static_cast<AppMode>(sel);
      menuScroll = (sel - firstMode) * 28 - 42;
    } else if (ev == BTN_EVT_UP) {
      sel = (sel <= firstMode) ? lastMode : (sel - 1);
      uiState.selectedMenuMode = static_cast<AppMode>(sel);
      menuScroll = (sel - firstMode) * 28 - 42;
    } else if (ev == BTN_EVT_RIGHT || ev == BTN_EVT_SELECT) {
      enterMode(uiState.selectedMenuMode);
    }
    return;
  }

  // Left or Select returns cleanly to Main Menu from sub-modes
  if (ev == BTN_EVT_LEFT) {
    enterMode(MODE_MENU);
    return;
  }

  switch (uiState.activeMode) {
    case MODE_WIFI_WIDS:  modWifi.handleInput(ev); break;
    case MODE_BLE_AUDIT:  modBle.handleInput(ev); break;
    case MODE_SUBGHZ_RF:  modSubGhz.handleInput(ev); break;
    case MODE_NRF24_SPEC: modNrf24.handleInput(ev); break;
    case MODE_GPS_LOG:    modGps.handleInput(ev); break;
    case MODE_IR_LAB:     modIr.handleInput(ev); break;
    case MODE_SD_RUNNER:  modSd.handleInput(ev); break;
    case MODE_SYS_INFO:
      if (ev == BTN_EVT_SELECT) enterMode(MODE_MENU);
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pixel.begin();
  pixel.setBrightness(40);
  pixel.setPixelColor(0, pixel.Color(0, 10, 20));
  pixel.show();

  if (!bus.begin()) {
    Serial.println("[ERR] BusManager init failed");
    while (true) {
      delay(1000);
    }
  }

  if (!gui.begin(bus)) {
    Serial.println("[ERR] AerodeckGui init failed");
    while (true) {
      delay(1000);
    }
  }

  refreshTelemetry();
  gui.drawSplashScreen();
  delay(800);
  enterMode(MODE_MENU);
  renderCurrentMode();
}

void loop() {
  bus.updateInputs();
  ButtonEvent ev = bus.consumeButtonEvent();
  if (ev != BTN_EVT_NONE) {
    dispatchModeInput(ev);
    renderCurrentMode();
    lastRenderMs = millis();
  }

  tickActiveModule();

  uint32_t now = millis();
  if (now - lastTelemetryMs >= 1000) {
    lastTelemetryMs = now;
    refreshTelemetry();
  }

  if (now - lastRenderMs >= 140) {
    lastRenderMs = now;
    renderCurrentMode();
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}
