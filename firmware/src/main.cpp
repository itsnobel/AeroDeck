#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config_pins.h"
#include "bus_manager.h"

static BusManager bus;
static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static uint32_t lastLogMs = 0;

void setup() {
  Serial.begin(115200);
  delay(250);

  pixel.begin();
  pixel.setBrightness(35);
  pixel.setPixelColor(0, pixel.Color(0, 20, 20));
  pixel.show();

  if (!bus.begin()) {
    Serial.println("[FAIL] BusManager initialization failed");
    pixel.setPixelColor(0, pixel.Color(30, 0, 0));
    pixel.show();
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[OK] Step 1 Sanity Check Ready (SPI2 mutex, CS HIGH, ADC1)");
}

void loop() {
  bus.updateInputs();
  ButtonEvent ev = bus.consumeButtonEvent();
  if (ev != BTN_EVT_NONE) {
    uint16_t mv = bus.readButtonMilliVolts();
    Serial.printf("[BTN] Event=%s (%u mV)\n", bus.buttonName(ev), static_cast<unsigned>(mv));
    pixel.setPixelColor(0, pixel.Color(0, 30, 10));
    pixel.show();
  }

  uint32_t now = millis();
  if (now - lastLogMs >= 1500) {
    lastLogMs = now;
    uint16_t btnMv = bus.readButtonMilliVolts();
    float batV = bus.readBatteryVolts();
    uint8_t batPct = bus.readBatteryPercent();
    Serial.printf("[TELEM] BtnADC=%u mV | Bat=%.2f V (%u%%) | CS(TFT/SD/CC/NRF)=%d/%d/%d/%d\n",
                  static_cast<unsigned>(btnMv),
                  batV,
                  static_cast<unsigned>(batPct),
                  digitalRead(PIN_TFT_CS),
                  digitalRead(PIN_SD_CS),
                  digitalRead(PIN_CC1101_CSN),
                  digitalRead(PIN_NRF24_CSN));
    pixel.setPixelColor(0, pixel.Color(0, 12, 20));
    pixel.show();
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}

  vTaskDelay(pdMS_TO_TICKS(10));
}
