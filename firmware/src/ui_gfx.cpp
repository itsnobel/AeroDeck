#include "ui_gfx.h"
#include <math.h>

LGFX_Aerodeck::LGFX_Aerodeck(void) {
  {
    auto cfg = _bus_instance.config();
    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = AERODECK_TFT_SPI_HZ;
    cfg.freq_read = 12000000;
    cfg.spi_3wire = false;
    cfg.use_lock = true;
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    cfg.pin_sclk = PIN_SPI_SCLK;
    cfg.pin_mosi = PIN_SPI_MOSI;
    cfg.pin_miso = PIN_SPI_MISO;
    cfg.pin_dc = PIN_TFT_DC;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);
  }

  {
    auto cfg = _panel_instance.config();
    cfg.pin_cs = PIN_TFT_CS;
    cfg.pin_rst = PIN_TFT_RST;
    cfg.pin_busy = -1;
    cfg.panel_width = TFT_W;
    cfg.panel_height = TFT_H;
    cfg.offset_x = AERODECK_TFT_OFFSET_X;
    cfg.offset_y = AERODECK_TFT_OFFSET_Y;
    cfg.offset_rotation = 0;
    cfg.dummy_read_pixel = 8;
    cfg.dummy_read_bits = 1;
    cfg.readable = false;
    cfg.invert = AERODECK_TFT_INVERT;
    cfg.rgb_order = false;
    cfg.dlen_16bit = false;
    cfg.bus_shared = true;
    _panel_instance.config(cfg);
  }

  setPanel(&_panel_instance);
}

AerodeckGui::AerodeckGui() : _strip(&_tft) {}

bool AerodeckGui::begin(BusManager& bus) {
  _bus = &bus;

  SpiGuard lock(*_bus);
  if (!lock.acquired()) {
    return false;
  }

  _bus->selectOnlyCs(PIN_TFT_CS);
  _tft.init();
  _tft.setRotation(0); // 128x160 portrait
  _tft.fillScreen(TFT_BLACK);

  _strip.setColorDepth(16);
  _stripReady = (_strip.createSprite(TFT_W, STRIP_H) != nullptr);
  if (!_stripReady) {
    _tft.setCursor(4, 24);
    _tft.setTextColor(TFT_RED, TFT_BLACK);
    _tft.print("Strip alloc fail!");
  }

  _bus->releaseAllCs();
  return _stripReady;
}

bool AerodeckGui::beginStrip(int16_t yTop, int16_t height, uint16_t bgColor) {
  if (!_stripReady || height <= 0) {
    return false;
  }
  if (height > STRIP_H) {
    height = STRIP_H;
  }
  if (_strip.width() != TFT_W || _strip.height() != height) {
    _strip.deleteSprite();
    if (_strip.createSprite(TFT_W, height) == nullptr) {
      _stripReady = false;
      return false;
    }
  }
  _strip.fillSprite(bgColor);
  (void)yTop;
  return true;
}

void AerodeckGui::pushStrip(int16_t x, int16_t yTop) {
  if (!_stripReady || !_bus) {
    return;
  }
  SpiGuard lock(*_bus);
  if (!lock.acquired()) {
    return;
  }
  _bus->selectOnlyCs(PIN_TFT_CS);
  _strip.pushSprite(&_tft, x, yTop);
  _bus->releaseAllCs();
}

const char* AerodeckGui::modeShortName(AppMode mode) {
  switch (mode) {
    case MODE_MENU:       return "MENU";
    case MODE_WIFI_WIDS:  return "WiFi6/WIDS";
    case MODE_BLE_AUDIT:  return "BLE AUDIT";
    case MODE_SUBGHZ_RF:  return "SUB-GHZ";
    case MODE_NRF24_SPEC: return "2.4G SPEC";
    case MODE_GPS_LOG:    return "GPS 5HZ";
    case MODE_IR_LAB:     return "IR LAB";
    case MODE_SD_RUNNER:  return "SD/JSON";
    case MODE_SYS_INFO:   return "SYS INFO";
    default:              return "AERODECK";
  }
}

uint16_t AerodeckGui::modeThemeColor(AppMode mode) {
  switch (mode) {
    case MODE_WIFI_WIDS:  return TFT_CYAN;
    case MODE_BLE_AUDIT:  return TFT_SKYBLUE;
    case MODE_SUBGHZ_RF:  return TFT_ORANGE;
    case MODE_NRF24_SPEC: return TFT_MAGENTA;
    case MODE_GPS_LOG:    return TFT_GREEN;
    case MODE_IR_LAB:     return TFT_RED;
    case MODE_SD_RUNNER:  return TFT_YELLOW;
    case MODE_SYS_INFO:   return TFT_WHITE;
    default:              return TFT_CYAN;
  }
}

void AerodeckGui::drawModeIcon(lgfx::LovyanGFX& dst, AppMode mode, int16_t x, int16_t y, int16_t size, uint16_t color) {
  int16_t cx = x + size / 2;
  int16_t cy = y + size / 2;
  int16_t r = size / 2 - 2;
  if (r < 4) r = 4;

  dst.drawRoundRect(x, y, size, size, 4, color);

  switch (mode) {
    case MODE_WIFI_WIDS: {
      dst.fillCircle(cx, cy + 4, 2, color);
      dst.drawArc(cx, cy + 4, r - 6, r - 7, 225, 315, color);
      dst.drawArc(cx, cy + 4, r - 2, r - 3, 225, 315, color);
      break;
    }
    case MODE_BLE_AUDIT: {
      dst.drawLine(cx - 3, cy - 5, cx + 3, cy + 1, color);
      dst.drawLine(cx + 3, cy + 1, cx - 3, cy + 6, color);
      dst.drawLine(cx - 3, cy + 6, cx - 3, cy - 5, color);
      dst.drawLine(cx - 3, cy - 5, cx + 3, cy - 2, color);
      dst.drawLine(cx + 3, cy - 2, cx - 3, cy + 2, color);
      break;
    }
    case MODE_SUBGHZ_RF: {
      dst.drawFastVLine(cx, cy - 5, 11, color);
      dst.drawCircle(cx, cy - 2, 3, color);
      dst.drawCircle(cx, cy - 2, 6, color);
      break;
    }
    case MODE_NRF24_SPEC: {
      int16_t bw = 3;
      dst.fillRect(cx - 6, cy + 1, bw, 5, color);
      dst.fillRect(cx - 1, cy - 4, bw, 10, color);
      dst.fillRect(cx + 4, cy - 1, bw, 7, color);
      break;
    }
    case MODE_GPS_LOG: {
      dst.drawCircle(cx, cy, r - 3, color);
      dst.drawLine(cx, cy, cx + 3, cy - 3, TFT_GREEN);
      dst.fillCircle(cx, cy, 2, color);
      break;
    }
    case MODE_IR_LAB: {
      dst.fillCircle(cx - 3, cy + 3, 2, color);
      dst.drawLine(cx - 1, cy + 1, cx + 5, cy - 5, color);
      dst.drawLine(cx + 1, cy + 3, cx + 6, cy - 2, color);
      break;
    }
    case MODE_SD_RUNNER: {
      dst.drawRect(cx - 4, cy - 5, 9, 11, color);
      dst.drawFastHLine(cx - 2, cy - 2, 5, color);
      dst.drawFastHLine(cx - 2, cy + 1, 5, color);
      break;
    }
    case MODE_SYS_INFO:
    default: {
      dst.drawCircle(cx, cy, 3, color);
      for (int deg = 0; deg < 360; deg += 60) {
        float rad = deg * 0.01745329f;
        int16_t gx = cx + (int16_t)(cosf(rad) * (r - 2));
        int16_t gy = cy + (int16_t)(sinf(rad) * (r - 2));
        dst.fillCircle(gx, gy, 1, color);
      }
      break;
    }
  }
}

void AerodeckGui::drawSplashScreen() {
  if (!_bus) return;

  {
    SpiGuard lock(*_bus);
    if (!lock.acquired()) return;
    _bus->selectOnlyCs(PIN_TFT_CS);
    _tft.fillScreen(TFT_BLACK);
    _tft.drawRect(3, 3, TFT_W - 6, TFT_H - 6, TFT_CYAN);
    _bus->releaseAllCs();
  }

  // Render upper emblem strip (y = 14..45)
  if (beginStrip(14, 32, TFT_BLACK)) {
    _strip.drawCircle(TFT_W / 2, 16, 12, TFT_CYAN);
    _strip.drawFastHLine(TFT_W / 2 - 15, 16, 30, TFT_DARKGREY);
    _strip.drawFastVLine(TFT_W / 2, 1, 30, TFT_DARKGREY);
    _strip.fillCircle(TFT_W / 2, 16, 3, TFT_GREEN);
    pushStrip(0, 14);
  }

  // Render title & subtitle strip (y = 54..85)
  if (beginStrip(54, 32, TFT_BLACK)) {
    _strip.setTextDatum(MC_DATUM);
    _strip.setTextColor(TFT_WHITE, TFT_BLACK);
    _strip.setFont(&fonts::Font2);
    _strip.drawString("AERODECK V2", TFT_W / 2, 10);
    _strip.setTextColor(TFT_CYAN, TFT_BLACK);
    _strip.setFont(&fonts::Font0);
    _strip.drawString("ESP32-C5 RF & LAB", TFT_W / 2, 24);
    pushStrip(0, 54);
  }

  // Render progress bar strip (y = 104..123)
  if (beginStrip(104, 20, TFT_BLACK)) {
    drawProgressBar(14, 4, TFT_W - 28, 10, 1.0f, TFT_CYAN, TFT_DARKGREY);
    pushStrip(0, 104);
  }
}

void AerodeckGui::drawTopBar(const UiStatusState& state) {
  if (!_bus) return;
  if (!beginStrip(0, TOP_BAR_H, TFT_NAVY)) return;

  _strip.setFont(&fonts::Font0);
  _strip.setTextDatum(ML_DATUM);
  _strip.setTextColor(TFT_WHITE, TFT_NAVY);

  // Left: active mode label
  _strip.drawString(modeShortName(state.activeMode), 4, TOP_BAR_H / 2);

  // Middle-right: GPS indicator pill
  int16_t gpsX = 74;
  uint16_t gpsCol = state.gpsFix ? TFT_GREEN : TFT_DARKGREY;
  _strip.fillCircle(gpsX, TOP_BAR_H / 2, 3, gpsCol);
  _strip.setTextColor(state.gpsFix ? TFT_GREEN : TFT_LIGHTGREY, TFT_NAVY);
  _strip.drawString("GPS", gpsX + 5, TOP_BAR_H / 2);

  // Far right: battery icon + percentage
  uint8_t pct = state.batteryPercent > 100 ? 100 : state.batteryPercent;
  char batBuf[6];
  snprintf(batBuf, sizeof(batBuf), "%u%%", pct);

  uint16_t batCol = TFT_GREEN;
  if (pct <= 15) batCol = TFT_RED;
  else if (pct <= 35) batCol = TFT_YELLOW;

  int16_t bx = TFT_W - 20;
  int16_t by = 5;
  _strip.drawRect(bx, by, 15, 8, TFT_WHITE);
  _strip.fillRect(bx + 15, by + 2, 2, 4, TFT_WHITE);
  int16_t fillW = (int16_t)(((11 * pct) + 50) / 100);
  if (fillW > 0) {
    _strip.fillRect(bx + 2, by + 2, fillW, 4, batCol);
  }

  _strip.setTextDatum(MR_DATUM);
  _strip.setTextColor(TFT_WHITE, TFT_NAVY);
  _strip.drawString(batBuf, bx - 2, TOP_BAR_H / 2);

  pushStrip(0, 0);
}

void AerodeckGui::drawBottomBar(const char* leftHint, const char* rightHint, uint16_t bgColor) {
  if (!_bus) return;
  if (!beginStrip(BOT_BAR_H == 16 ? (TFT_H - BOT_BAR_H) : 144, BOT_BAR_H, bgColor)) return;

  _strip.setFont(&fonts::Font0);
  _strip.setTextColor(TFT_WHITE, bgColor);

  if (leftHint && leftHint[0] != '\0') {
    _strip.setTextDatum(ML_DATUM);
    _strip.drawString(leftHint, 4, BOT_BAR_H / 2);
  }
  if (rightHint && rightHint[0] != '\0') {
    _strip.setTextDatum(MR_DATUM);
    _strip.drawString(rightHint, TFT_W - 4, BOT_BAR_H / 2);
  }

  pushStrip(0, TFT_H - BOT_BAR_H);
}

void AerodeckGui::drawMainMenu(const UiStatusState& state, int16_t scrollOffset) {
  drawTopBar(state);

  // Menu items are modes 1 .. (MODE_COUNT - 1)
  static constexpr int16_t ITEM_H = 28;
  static constexpr int16_t VISIBLE_H = BODY_Y_END - BODY_Y_START; // 126 px
  const int itemCount = static_cast<int>(MODE_COUNT) - 1;
  const int16_t totalH = static_cast<int16_t>(itemCount) * ITEM_H;

  int16_t maxScroll = totalH - VISIBLE_H;
  if (maxScroll < 0) maxScroll = 0;
  if (scrollOffset < 0) scrollOffset = 0;
  if (scrollOffset > maxScroll) scrollOffset = maxScroll;

  // Render body in 28px/32px strips across y = 18..143
  int16_t y = BODY_Y_START;
  while (y < BODY_Y_END) {
    int16_t h = BODY_Y_END - y;
    if (h > STRIP_H) h = STRIP_H;

    if (beginStrip(y, h, TFT_BLACK)) {
      _strip.setFont(&fonts::Font0);
      _strip.setTextDatum(ML_DATUM);

      for (int idx = 0; idx < itemCount; ++idx) {
        AppMode m = static_cast<AppMode>(idx + 1);
        int16_t itemScreenY = BODY_Y_START + static_cast<int16_t>(idx) * ITEM_H - scrollOffset;
        int16_t itemBottom = itemScreenY + ITEM_H;

        if (itemBottom <= y || itemScreenY >= (y + h)) {
          continue;
        }

        int16_t localY = itemScreenY - y;
        bool selected = (m == state.selectedMenuMode);
        uint16_t accent = modeThemeColor(m);

        if (selected) {
          _strip.fillRoundRect(2, localY + 1, TFT_W - 4, ITEM_H - 2, 4, 0x18E3);
          _strip.drawRoundRect(2, localY + 1, TFT_W - 4, ITEM_H - 2, 4, accent);
        }

        drawModeIcon(_strip, m, 6, localY + 4, 20, accent);
        _strip.setTextColor(selected ? TFT_WHITE : TFT_LIGHTGREY, selected ? 0x18E3 : TFT_BLACK);
        _strip.drawString(modeShortName(m), 32, localY + ITEM_H / 2);
      }

      pushStrip(0, y);
    }
    y += h;
  }

  drawBottomBar("UP/DN:Move", "SEL:Open", TFT_NAVY);
}

void AerodeckGui::drawToast(const char* title, const char* message, uint16_t accentColor, uint16_t holdMs) {
  if (!_bus) return;
  if (beginStrip(56, 48, 0x1082)) {
    _strip.drawRoundRect(4, 2, TFT_W - 8, 44, 4, accentColor);
    _strip.setFont(&fonts::Font0);
    _strip.setTextDatum(MC_DATUM);
    _strip.setTextColor(accentColor, 0x1082);
    _strip.drawString(title ? title : "NOTICE", TFT_W / 2, 13);
    _strip.setTextColor(TFT_WHITE, 0x1082);
    _strip.drawString(message ? message : "", TFT_W / 2, 30);
    pushStrip(0, 56);
  }
  if (holdMs > 0) {
    vTaskDelay(pdMS_TO_TICKS(holdMs));
  }
}

void AerodeckGui::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float ratio, uint16_t fgColor, uint16_t bgColor) {
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  _strip.fillRoundRect(x, y, w, h, 3, bgColor);
  _strip.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  int16_t innerW = w - 4;
  if (innerW > 0) {
    int16_t fillW = static_cast<int16_t>(innerW * ratio);
    if (fillW > 0) {
      _strip.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, fgColor);
    }
  }
}

void AerodeckGui::drawSignalMeter(int16_t x, int16_t y, int16_t w, int16_t h, int rssiDbm, uint16_t bgColor) {
  // Map typical RF RSSI [-100 dBm .. -30 dBm] -> [0.0 .. 1.0]
  float norm = (static_cast<float>(rssiDbm) + 100.0f) / 70.0f;
  uint16_t col = TFT_RED;
  if (rssiDbm >= -55) col = TFT_GREEN;
  else if (rssiDbm >= -75) col = TFT_YELLOW;
  else if (rssiDbm >= -88) col = TFT_ORANGE;

  drawProgressBar(x, y, w, h, norm, col, bgColor);
}

void AerodeckGui::drawScrollList(int16_t yStart, int16_t yEnd, const char* const* items, size_t count, size_t selectedIndex, size_t& scrollTop) {
  if (!items || count == 0 || yEnd <= yStart) return;

  static constexpr size_t ROW_H = 14;
  size_t visibleRows = static_cast<size_t>((yEnd - yStart) / static_cast<int16_t>(ROW_H));
  if (visibleRows < 1) visibleRows = 1;

  if (selectedIndex >= count) selectedIndex = count - 1;
  if (selectedIndex < scrollTop) {
    scrollTop = selectedIndex;
  } else if (selectedIndex >= scrollTop + visibleRows) {
    scrollTop = selectedIndex - visibleRows + 1;
  }

  int16_t y = yStart;
  while (y < yEnd) {
    int16_t h = yEnd - y;
    if (h > STRIP_H) h = STRIP_H;

    if (beginStrip(y, h, TFT_BLACK)) {
      _strip.setFont(&fonts::Font0);
      _strip.setTextDatum(ML_DATUM);

      for (size_t i = scrollTop; i < count && i < scrollTop + visibleRows + 2; ++i) {
        int16_t rowScreenY = yStart + static_cast<int16_t>((i - scrollTop) * ROW_H);
        int16_t rowBottom = rowScreenY + static_cast<int16_t>(ROW_H);
        if (rowBottom <= y || rowScreenY >= (y + h)) continue;

        int16_t localY = rowScreenY - y;
        bool isSel = (i == selectedIndex);
        if (isSel) {
          _strip.fillRect(0, localY, TFT_W, static_cast<int16_t>(ROW_H), 0x18E3);
        }
        _strip.setTextColor(isSel ? TFT_CYAN : TFT_LIGHTGREY, isSel ? 0x18E3 : TFT_BLACK);
        _strip.drawString(items[i] ? items[i] : "", 4, localY + static_cast<int16_t>(ROW_H) / 2);
      }

      pushStrip(0, y);
    }
    y += h;
  }
}
