#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config_pins.h"
#include "bus_manager.h"
#include "ui_gfx.h"

static BusManager bus;
static AerodeckGui gui;
static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

static UiStatusState uiState;
static int16_t menuScroll = 0;
static bool needsRedraw = true;
static uint32_t lastTelemetryMs = 0;

static void applyNeoPixelForMode(AppMode mode) {
  uint32_t col = pixel.Color(0, 20, 20);
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

static void refreshTelemetry() {
  uiState.batteryVolts = bus.readBatteryVolts();
  uiState.batteryPercent = bus.readBatteryPercent();
  // GPS fix state will be wired in Step 7 (mod_gps)
}

static void drawActiveScreen() {
  if (uiState.activeMode == MODE_MENU) {
    gui.drawMainMenu(uiState, menuScroll);
  } else {
    gui.drawTopBar(uiState);

    if (gui.beginStrip(BODY_Y_START, STRIP_H, TFT_BLACK)) {
      gui.strip().setFont(&fonts::Font0);
      gui.strip().setTextDatum(MC_DATUM);
      gui.strip().setTextColor(AerodeckGui::modeThemeColor(uiState.activeMode), TFT_BLACK);
      gui.strip().drawString(AerodeckGui::modeShortName(uiState.activeMode), TFT_W / 2, 10);
      gui.strip().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      gui.strip().drawString("Module in next step", TFT_W / 2, 23);
      gui.pushStrip(0, BODY_Y_START);
    }

    gui.drawBottomBar("LEFT/SEL:Back", "", TFT_NAVY);
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
  delay(900);
  applyNeoPixelForMode(uiState.activeMode);
  needsRedraw = true;
}

void loop() {
  bus.updateInputs();
  ButtonEvent ev = bus.consumeButtonEvent();

  uint32_t now = millis();
  if (now - lastTelemetryMs >= 2000) {
    lastTelemetryMs = now;
    uint8_t oldPct = uiState.batteryPercent;
    refreshTelemetry();
    if (oldPct != uiState.batteryPercent) {
      needsRedraw = true;
    }
  }

  if (ev != BTN_EVT_NONE) {
    const int firstMode = static_cast<int>(MODE_WIFI_WIDS);
    const int lastMode = static_cast<int>(MODE_COUNT) - 1;
    int sel = static_cast<int>(uiState.selectedMenuMode);

    if (uiState.activeMode == MODE_MENU) {
      if (ev == BTN_EVT_DOWN) {
        sel = (sel >= lastMode) ? firstMode : (sel + 1);
        uiState.selectedMenuMode = static_cast<AppMode>(sel);
        menuScroll = (sel - firstMode) * 28 - 42;
        needsRedraw = true;
      } else if (ev == BTN_EVT_UP) {
        sel = (sel <= firstMode) ? lastMode : (sel - 1);
        uiState.selectedMenuMode = static_cast<AppMode>(sel);
        menuScroll = (sel - firstMode) * 28 - 42;
        needsRedraw = true;
      } else if (ev == BTN_EVT_RIGHT || ev == BTN_EVT_SELECT) {
        uiState.activeMode = uiState.selectedMenuMode;
        applyNeoPixelForMode(uiState.activeMode);
        needsRedraw = true;
      }
    } else {
      if (ev == BTN_EVT_LEFT || ev == BTN_EVT_SELECT) {
        uiState.activeMode = MODE_MENU;
        applyNeoPixelForMode(uiState.activeMode);
        needsRedraw = true;
      }
    }
  }

  if (needsRedraw) {
    drawActiveScreen();
    needsRedraw = false;
  }

  vTaskDelay(pdMS_TO_TICKS(15));
}
