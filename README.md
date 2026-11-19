
# Aerodeck 
This device allows auditing of cybersecurity and performing RF telemetry. It uses an ESP32-C5 chip, allowing for dual band (2.4GHz/5GHz) Wi-Fi 6 and BLE 5 communication. The purpose of this project was to understand wireless communication methods, sub GHz communication, IR remotes and GPS wardriving on hardware level, without spending hundreds of dollars.

## Why I Built V2 (V1 vs. V2 Comparison)

- Although the Aerodeck V1 looked good, the manual assembly process was very cumbersome. Therefore, Aerodeck V2 has been redesigned in order to be more compact, easy to solder, and powerful.

- Key Differences in Controller
Aerodeck V1: Difficult to manually solder the components (QFN) and therefore risky. The device only contains an ESP32-C5. Aerodeck V2: Contains an ESP32-C5-DevKit V2.0. This makes it easier to programme using a USB to UART connection. It also includes a NeoPixel LED, and stable power regulation.

- Power/Charging
Aerodeck V1: Complex charging circuitry and 3.3v step down circuit. Increases complexity of the PCB. Grounding could be problematic. Aerodeck V2: Off-board 3 pin 5v module (TP4056 + MT3608 + Latching Switch + 1N5819). This removes complexity in the PCB and allows proper isolation for battery safety.

- Screen and Storage
Aerodeck V1: 0.96” OLED screen. Requires an external SD card. Takes up more board space and increases wiring complexity. Aerodeck V2: 1.8” ST7735 Colour TFT Screen. Internal Micro SD card. Less board space used. Enables real time waterfall charts and meters.

- Navigation
Aerodeck V1: 5 separate buttons connected via 5 GPIO pins. Aerodeck V2: 5 buttons connected via a single GPIO pin using a resistor chain. 4 GPIO pins saved, with safe voltages.

- RF/Sensors
Aerodeck V1: Only CC1101 sub-gigahertz module available. Aerodeck V2: Dual band Wi-Fi 6, 2.4GHz NRF24, Sub-GHz, IR, and 5Hz GPS support.

---

## Visual Comparison: V1 vs. V2 Design Placeholders

### 1. Schematic Comparison
* **Aerodeck V1 Schematic:**
    <img width="1001" height="693" alt="Screenshot 2026-10-11 003418" src="https://github.com/user-attachments/assets/96513c36-19f5-4ac3-922e-22a86ab02f51" />

    <img width="1124" height="730" alt="Screenshot 2026-10-09 165233" src="https://github.com/user-attachments/assets/6b0b28b1-cabb-4777-b77c-44d08f9ab4ae" />

* **Aerodeck V2 Schematic:**
  <img width="999" height="715" alt="Screenshot 2026-11-17 094346" src="https://github.com/user-attachments/assets/fcf96505-4e95-4723-83b5-9d0a3d93357c" />

  <img width="1455" height="942" alt="Screenshot 2026-11-14 235020" src="https://github.com/user-attachments/assets/263833dd-8b98-4b74-9a1a-63f7b709c8d7" />


### 2. PCB Design and Routing Comparison
* **Aerodeck V1 PCB Design:**
  <img width="1386" height="938" alt="Screenshot 2026-10-13 091513" src="https://github.com/user-attachments/assets/7fe98b5d-4317-4ac4-ab2a-7d56045bbf3b" />

* **Aerodeck V2 PCB Design:**

  <img width="687" height="759" alt="image" src="https://github.com/user-attachments/assets/e29cec05-d992-47d3-99d5-4f3c5f75f971" />


### 3. 2D PCB View Comparison
* **Aerodeck V1 2D View:**
  <img width="854" height="674" alt="Screenshot 2026-10-12 224936" src="https://github.com/user-attachments/assets/7d5644d4-d5e3-4305-9632-abbd39d61c87" />


* **Aerodeck V2 2D View:**


  <img width="728" height="747" alt="Screenshot 2026-11-18 122935" src="https://github.com/user-attachments/assets/a084a8a3-0ea0-4890-82c6-77600e03e258" />

  <img width="730" height="739" alt="Screenshot 2026-11-18 122959" src="https://github.com/user-attachments/assets/66a675a3-5d66-4dd4-883d-80012061e9ea" />

* **Aerodeck V2 3D View:**
  <img width="1001" height="774" alt="Screenshot 2026-11-18 123102" src="https://github.com/user-attachments/assets/756781cf-225c-4413-a9f4-d1bf091796c3" />
  <img width="879" height="770" alt="Screenshot 2026-11-18 123210" src="https://github.com/user-attachments/assets/17443928-11bc-425a-a08f-242e8fc8808b" />


---

## What Aerodeck V2 Can Do

Wi-Fi (Dual-band) + Bluetooth (ESP32-C5)
Scans both 2.4GHz and 5GHz. Listens for WIDS alerts. Captures packets and stores them on a microSD card. Can view BLE advertising.
Sub-1GHz Lab (CC1101)
Scans frequencies in 315/433/868/915 range. Live waterfall chart is displayed on the TFT showing the strength of signals in real time. Raw pulse timing data can be decoded.
2.4GHz Spectrum Analyser (NRF24L01+ PA+LNA)
Live scan of 2400 to 2525MHz displaying interference/noise.
5Hz GPS (ATK-NEO-6M)
Speed, location and height measured. Maps can be generated using the KML/CSV outputs.
IR Lab
Can capture, decode and transmit IR remote profiles. Uses a 940nm IR LED and 2N2222A transistor.
Custom UI
LovyanGFX library used for nice UI. Battery level is shown in real time. Files in SD card can be navigated through.

---

## Special PCB feature

Single pin, 5 pushbuttons Using a resistor ladder, safe high voltage readings can be taken from 5 navigation buttons connected to GPIO5.

SPI bus shared between devices with strapping protection The SPI buses used by the TFT, SD, CC1101 and NRF24L01+ modules are shared. Pull up resistors are used on all Chip Select lines.
Zero voltage drop A 5V voltage is maintained using the MT3608 boost module and a 1N5819 Schottky diode.
Strong decoupling of RF circuitry To stop brown-out resets while transmitting, capacitor pairs are placed directly adjacent to the power rails.

---

## BOM
## Bill of Materials (BOM)

| # | Item | Qty | Supplier | Cost / Item | Total Cost | Link |
| :-: | :--- | :-: | :--- | :---: | :---: | :--- |
| 1 | ESP32-C5-DevKit V2.0 | 1 | RoboticsBD | $11.98 | $11.98 | [Link](https://store.roboticsbd.com/internet-of-things-iot/4031-esp32-c5-dual-band-wifi-6-development-board-robotics-bangladesh.html) |
| 2 | CC1101 Sub-1GHz RF Module | 1 | RoboticsBD | $5.55 | $5.55 | [Link](https://store.roboticsbd.com/communication-module/2359-cc1101-500kbps-wireless-transceiver-module-rf-433mhz-spi-interface-sma-antenna-robotics-bangladesh.html) |
| 3 | 1.8" ST7735 SPI TFT (with SD slot) | 1 | RoboticsBD | $4.81 | $4.81 | [Link](https://store.roboticsbd.com/display/699-tft-lcd-display-module-st7735s-128x160-robotics-bangladesh.html) |
| 4 | TP4056 LiPo Battery Charger Module | 1 | RoboticsBD | $0.30 | $0.30 | [Link](https://store.roboticsbd.com/robotics-parts/919-tp4056-lithium-battery-charger-module-with-protection-dual-functions-robotics-bangladesh.html) |
| 5 | 3.7V Battery | 1 | RoboticsBD | $2.25 | $2.25 | [Link](https://store.roboticsbd.com/battery-charger/3085-18650-geepas-37v-3000mah-li-ion-battery-button-top-robotics-bangladesh.html) |
| 6 | Passive Components (Resistors, Capacitors, Switch) | 5 | RoboticsBD | $0.12 | $0.60 | [Link](https://store.roboticsbd.com/resistor/243-10k-ohm-carbon-film-resistor-pack-of-10-robotics-bangladesh.html) |
| 7 | NRF24L01+ PA+LNA Module | 1 | RoboticsBD | $2.72 | $2.72 | [Link](https://store.roboticsbd.com/electronics-module/400-nrf24l01palna-24ghz-sma-antenna-wireless-transceiver-module-robotics-bangladesh.html) |
| 8 | ATK-NEO-6M GPS Module | 1 | RoboticsBD | $3.86 | $3.86 | [Link](https://store.roboticsbd.com/electronics-module/797-u-blox-neo-6m-gps-module-robotics-bangladesh.html) |
| 9 | Tactile Push buttons | 5 | RoboticsBD | $0.05 | $0.25 | [Link](https://store.roboticsbd.com/electronic-switches/1415-push-button-4-pin-robotics-bangladesh.html) |
| 10 | VS1838B + 5mm 940nm IR LED + 2N2222A | 3 | RoboticsBD | $0.18 | $0.54 | [Link](https://store.roboticsbd.com/sensors/1191-ir-receiver-shielded-robotics-bangladesh.html) |
| 11 | MT3608 | 1 | RoboticsBD | $0.65 | $0.65 | [Link](https://store.roboticsbd.com/power-module-adapter/3841-mt3608-type-c-dc-dc-step-up-boost-converter-module-robotics-bangladesh.html) |
| 12 | Estimated total shipping | 1 | — | — | $2.00 | — |
| | **Grand Total** | | | | **$35.51** | |
---

## Repository Structure
* /pcb_V2 — The EasyEDA schematic and board layout source files(.json) of the main/latest model.
* /pcb_V1 — The EasyEDA schematic and board layout source files(.json) from previous model.
* /GERBER — Manufacturing-ready Gerber export packages(.zip).
* /firmware — Starter code, pin definitions, and hardware initialization scripts for the ESP32-C5.

**Encloser for AERODECK will be made in week-2 CAD project in HalfLifew**

*Built as an open-source educational hardware project for Hack Club.*

