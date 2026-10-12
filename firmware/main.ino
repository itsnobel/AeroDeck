/*
 * AeroDeck-3S — Main Firmware Skeleton
 * Microcontroller: ESP32-C5-WROOM-1
 * Hardware Test File for Hack Club PCB Submission
 */

#include <Wire.h>
#include <SPI.h>

// I2C Pins for 0.96" OLED Display
#define OLED_SDA      21
#define OLED_SCL      22

// Shared SPI Bus Pins for CC1101 Sub-1GHz Module & MicroSD
#define SPI_SCK       18
#define SPI_MISO      19
#define SPI_MOSI      23
#define CC1101_CS     5
#define SD_CS         13

// Analog Pins for 5-Way Joystick & Push Buttons
#define JOYSTICK_X    34
#define JOYSTICK_Y    35
#define JOYSTICK_BTN  32
#define BOOT_BTN      0

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing AeroDeck-3S Hardware Interfaces...");

  // Initialize I2C Display Bus
  Wire.begin(OLED_SDA, OLED_SCL);

  // Initialize SPI Radio & Storage Bus
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  pinMode(CC1101_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  // Initialize Input Controls
  pinMode(JOYSTICK_BTN, INPUT_PULLUP);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  Serial.println("AeroDeck-3S Hardware Ready!");
}

void loop() {
  // Placeholder loop for multi-protocol RF auditing routines
  delay(1000);
}
