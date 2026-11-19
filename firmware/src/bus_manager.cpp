#include "bus_manager.h"
#include <driver/gpio.h>

BusManager::BusManager() {
  _spiMutex = xSemaphoreCreateMutex();
}

BusManager::~BusManager() {
  if (_spiMutex != nullptr) {
    vSemaphoreDelete(_spiMutex);
    _spiMutex = nullptr;
  }
}

void BusManager::remapUsbJtagPins() {
  // Detach GPIO13 and GPIO14 from USB Serial/JTAG function so they work as clean GPIO
  gpio_reset_pin(static_cast<gpio_num_t>(PIN_IR_TX));
  gpio_reset_pin(static_cast<gpio_num_t>(PIN_SD_CS));
}

void BusManager::initAllCsHigh() {
  const int csPins[] = {PIN_TFT_CS, PIN_SD_CS, PIN_CC1101_CSN, PIN_NRF24_CSN};
  for (int pin : csPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  pinMode(PIN_NRF24_CE, OUTPUT);
  digitalWrite(PIN_NRF24_CE, LOW);
}

void BusManager::releaseAllCs() {
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_SD_CS, HIGH);
  digitalWrite(PIN_CC1101_CSN, HIGH);
  digitalWrite(PIN_NRF24_CSN, HIGH);
}

void BusManager::selectOnlyCs(int activeCsPin) {
  releaseAllCs();
  if (activeCsPin == PIN_TFT_CS ||
      activeCsPin == PIN_SD_CS ||
      activeCsPin == PIN_CC1101_CSN ||
      activeCsPin == PIN_NRF24_CSN) {
    digitalWrite(activeCsPin, LOW);
  }
}

bool BusManager::begin() {
  if (_spiMutex == nullptr) {
    _spiMutex = xSemaphoreCreateMutex();
    if (_spiMutex == nullptr) return false;
  }

  remapUsbJtagPins();
  initAllCsHigh();

  analogSetAttenuation(ADC_11db);
  pinMode(PIN_BTN_ADC, ANALOG);
  pinMode(PIN_BAT_ADC, ANALOG);

  if (lockSpi(pdMS_TO_TICKS(1000))) {
    if (!_spiStarted) {
      SPI.begin(PIN_SPI_SCLK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
      _spiStarted = true;
    }
    releaseAllCs();
    unlockSpi();
  } else {
    return false;
  }

  return true;
}

bool BusManager::lockSpi(TickType_t timeoutTicks) {
  if (_spiMutex == nullptr) return false;
  return (xSemaphoreTake(_spiMutex, timeoutTicks) == pdTRUE);
}

bool BusManager::tryLockSpi(uint32_t timeoutMs) {
  return lockSpi(pdMS_TO_TICKS(timeoutMs));
}

void BusManager::unlockSpi() {
  if (_spiMutex != nullptr) {
    xSemaphoreGive(_spiMutex);
  }
}

uint16_t BusManager::readButtonMilliVolts() {
  // Take 3 samples and pick median to suppress ADC / filter ripple
  uint32_t s[3];
  for (int i = 0; i < 3; ++i) {
    s[i] = analogReadMilliVolts(PIN_BTN_ADC);
  }
  if (s[0] > s[1]) { uint32_t t = s[0]; s[0] = s[1]; s[1] = t; }
  if (s[1] > s[2]) { uint32_t t = s[1]; s[1] = s[2]; s[2] = t; }
  if (s[0] > s[1]) { uint32_t t = s[0]; s[0] = s[1]; s[1] = t; }
  return static_cast<uint16_t>(s[1]);
}

ButtonEvent BusManager::classifyButtonVoltage(uint16_t mv) const {
  // Nominal ladder nodes: SELECT=0mV, LEFT=595mV, UP=1171mV, RIGHT=1666mV, DOWN=2207mV, IDLE=3300mV
  if (mv < 280)  return BTN_EVT_SELECT;
  if (mv < 880)  return BTN_EVT_LEFT;
  if (mv < 1420) return BTN_EVT_UP;
  if (mv < 1935) return BTN_EVT_RIGHT;
  if (mv < 2750) return BTN_EVT_DOWN;
  return BTN_EVT_NONE;
}

void BusManager::updateInputs() {
  uint16_t mv = readButtonMilliVolts();
  ButtonEvent raw = classifyButtonVoltage(mv);
  uint32_t now = millis();

  if (raw != _lastRawBtn) {
    _lastRawBtn = raw;
    _lastChangeMs = now;
  }

  static constexpr uint32_t DEBOUNCE_MS = 35;
  if ((now - _lastChangeMs) >= DEBOUNCE_MS && raw != _debouncedBtn) {
    _debouncedBtn = raw;
    if (_debouncedBtn != BTN_EVT_NONE) {
      _pendingEvent = _debouncedBtn;
    }
  }
}

ButtonEvent BusManager::consumeButtonEvent() {
  ButtonEvent ev = _pendingEvent;
  _pendingEvent = BTN_EVT_NONE;
  return ev;
}

const char* BusManager::buttonName(ButtonEvent ev) const {
  switch (ev) {
    case BTN_EVT_SELECT: return "SELECT";
    case BTN_EVT_LEFT:   return "LEFT";
    case BTN_EVT_UP:     return "UP";
    case BTN_EVT_RIGHT:  return "RIGHT";
    case BTN_EVT_DOWN:   return "DOWN";
    default:             return "NONE";
  }
}

float BusManager::readBatteryVolts() {
  uint32_t sumMv = 0;
  for (int i = 0; i < 8; ++i) {
    sumMv += analogReadMilliVolts(PIN_BAT_ADC);
  }
  float avgPinVolts = (static_cast<float>(sumMv) / 8.0f) / 1000.0f;
  return avgPinVolts * 2.0f; // 100k/100k 1:1 divider
}

uint8_t BusManager::readBatteryPercent() {
  float v = readBatteryVolts();
  if (v <= BAT_VOLT_MIN) return 0;
  if (v >= BAT_VOLT_MAX) return 100;
  float pct = ((v - BAT_VOLT_MIN) / (BAT_VOLT_MAX - BAT_VOLT_MIN)) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return static_cast<uint8_t>(pct + 0.5f);
}
