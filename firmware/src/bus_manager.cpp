// ============================================================================
// Aerodeck V2 - Shared SPI2 Bus Manager (implementation)
// ============================================================================
#include "bus_manager.h"
#include "config_pins.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_rom_gpio.h>
#include <driver/gpio.h>

#if __has_include(<driver/usb_serial_jtag.h>)
  #include <driver/usb_serial_jtag.h>
  #define HAS_USB_SERIAL_JTAG_DRIVER 1
#else
  #define HAS_USB_SERIAL_JTAG_DRIVER 0
#endif

// Hard guard: Aerodeck V2 requires UART0 console on GPIO11/12 so GPIO13/14
// can serve as IR_TX / SD_CS. If someone flips this flag, fail loudly.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  #error "Aerodeck V2 requires -DARDUINO_USB_CDC_ON_BOOT=0 (UART0 console on GPIO11/12)."
#endif

namespace BusManager {

static SemaphoreHandle_t s_spiMutex = nullptr;
static bool              s_ready    = false;

static const uint8_t kCsPins[] = {
    PIN_TFT_CS,
    PIN_SD_CS,
    PIN_CC_CSN,
    PIN_NRF_CSN,
};
static constexpr size_t kNumCs = sizeof(kCsPins) / sizeof(kCsPins[0]);

// ----------------------------------------------------------------------------
// Reclaim GPIO13 (USB D-) and GPIO14 (USB D+) from the native USB Serial/JTAG
// peripheral so they can serve as IR_TX and SD_CS.
//
// Safe when the driver was never installed: usb_serial_jtag_driver_uninstall()
// returns ESP_ERR_INVALID_STATE in that case, which we ignore.
// ----------------------------------------------------------------------------
static void reconfigureUsbPins() {
#if HAS_USB_SERIAL_JTAG_DRIVER
    (void)usb_serial_jtag_driver_uninstall();
#endif

    // Force the IO MUX to route these pads to the GPIO peripheral.
    esp_rom_gpio_pad_select_gpio((uint32_t)PIN_IR_TX);
    esp_rom_gpio_pad_select_gpio((uint32_t)PIN_SD_CS);

    // Drive safe default levels: IR LED off, SD deselected.
    pinMode(PIN_IR_TX, OUTPUT);
    digitalWrite(PIN_IR_TX, LOW);

    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
}

// ----------------------------------------------------------------------------
// Drive every CS HIGH and set safe defaults on the other control pins.
// MUST be called before SPI.begin() to satisfy strapping-pin requirements.
// ----------------------------------------------------------------------------
static void initControlPins() {
    for (size_t i = 0; i < kNumCs; i++) {
        pinMode(kCsPins[i], OUTPUT);
        digitalWrite(kCsPins[i], HIGH);
    }

    pinMode(PIN_NRF_CE, OUTPUT);
    digitalWrite(PIN_NRF_CE, LOW);   // standby

    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH); // out of reset

    pinMode(PIN_CC_GDO0, INPUT);     // RSSI / packet-ready
    pinMode(PIN_IR_RX, INPUT);       // VS1838B
}

// ----------------------------------------------------------------------------
bool begin() {
    if (s_ready) return true;

    // 1. Reclaim USB pads FIRST, before anything else touches them.
    reconfigureUsbPins();

    // 2. All CS HIGH before SPI init (strapping-pin safety).
    initControlPins();

    // 3. Mutex.
    s_spiMutex = xSemaphoreCreateMutex();
    if (s_spiMutex == nullptr) {
        return false;
    }

    // 4. Shared SPI2 bus. SS = -1 because we manage CS manually.
    SPI.begin(PIN_SPI_SCLK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

    // 5. Defensive re-assert (some cores briefly toggle SS during begin).
    csAllHigh();

    s_ready = true;
    return true;
}

// ----------------------------------------------------------------------------
bool lock(uint32_t timeoutMs) {
    if (s_spiMutex == nullptr) return false;
    return xSemaphoreTake(s_spiMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void unlock() {
    if (s_spiMutex != nullptr) {
        xSemaphoreGive(s_spiMutex);
    }
}

// ----------------------------------------------------------------------------
SPIClass& spi() { return SPI; }

// ----------------------------------------------------------------------------
void csAllHigh() {
    for (size_t i = 0; i < kNumCs; i++) {
        digitalWrite(kCsPins[i], HIGH);
    }
}
void csLow(uint8_t pin)  { digitalWrite(pin, LOW);  }
void csHigh(uint8_t pin) { digitalWrite(pin, HIGH); }

// ----------------------------------------------------------------------------
void beginTransaction(const SPISettings& settings) { SPI.beginTransaction(settings); }
void endTransaction()                              { SPI.endTransaction();        }

// ----------------------------------------------------------------------------
bool isReady() { return s_ready; }

// ----------------------------------------------------------------------------
void printDiagnostics() {
    Serial.println("[BUS] ---- Diagnostics ----");
    Serial.printf("[BUS] ready=%d  mutex=%p  freeHeap=%u  minFreeHeap=%u\n",
                  (int)s_ready, (void*)s_spiMutex,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap());

    Serial.printf("[BUS] CS: TFT=%d SD=%d CC=%d NRF=%d\n",
                  digitalRead(PIN_TFT_CS),
                  digitalRead(PIN_SD_CS),
                  digitalRead(PIN_CC_CSN),
                  digitalRead(PIN_NRF_CSN));

    Serial.printf("[BUS] NRF_CE=%d TFT_RST=%d IR_TX=%d IR_RX=%d CC_GDO0=%d\n",
                  digitalRead(PIN_NRF_CE),
                  digitalRead(PIN_TFT_RST),
                  digitalRead(PIN_IR_TX),
                  digitalRead(PIN_IR_RX),
                  digitalRead(PIN_CC_GDO0));

    Serial.printf("[BUS] SPI2 pins: SCLK=%d MOSI=%d MISO=%d\n",
                  PIN_SPI_SCLK, PIN_SPI_MOSI, PIN_SPI_MISO);
    Serial.println("[BUS] ------------------------");
}

} // namespace BusManager
