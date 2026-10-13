# ESP32 Smart Clock with 3.5" TFT & Server Monitor

An all-in-one desktop dashboard built with an ESP32. This project features a real-time clock synchronized via NTP, live weather updates using the OpenWeather API, a dual-alarm system connected to a relay, and a local network server monitor.

## 🌟 Features
*   **NTP Real-Time Clock**: Accurate timekeeping synced over WiFi.
*   **Weather Dashboard**: Displays current temperature and weather conditions (via OpenWeather API).
*   **Network Monitor**: Continuously pings and displays the online/offline status of local servers (e.g., Gateway, STB HG680P, TrueNAS) and external internet (Google).
*   **Dual Alarm System**: Configurable alarms that trigger a hardware relay (can be attached to a buzzer, light, or other appliances).
*   **Interactive UI**: Clean, custom-drawn interface on a 3.5" TFT display controlled via 5 push buttons.

## 🛠️ Hardware Requirements
*   ESP32 Development Board
*   3.5" TFT LCD Display (compatible with `TFT_eSPI` library)
*   Relay Module (Active LOW / HIGH configurable)
*   5x Push Buttons (Set, Up, Down, Left, Right)
*   10kΩ Pull-up resistors (for input-only pins like 34 and 35)

## 📌 Pin Configuration

### Display Pins (8-bit Parallel)
| Function | ESP32 Pin |
| :--- | :--- |
| TFT_RST | `26` |
| TFT_CS | `27` |
| TFT_DC | `14` |
| TFT_WR | `12` |
| TFT_RD | `13` |
| TFT_D0 | `16` |
| TFT_D1 | `4` |
| TFT_D2 | `23` |
| TFT_D3 | `22` |
| TFT_D4 | `21` |
| TFT_D5 | `19` |
| TFT_D6 | `18` |
| TFT_D7 | `17` |

### Control & Button Pins
| Component | ESP32 Pin | Note |
| :--- | :--- | :--- |
| Button SET | `32` | Internal Pull-up |
| Button UP | `33` | Internal Pull-up |
| Button DOWN | `25` | Internal Pull-up |
| Button LEFT | `34` | **Requires external 10k pull-up** |
| Button RIGHT| `35` | **Requires external 10k pull-up** |
| Relay Output| `15` | Triggers on Alarm |
*(Note: TFT display pins are configured inside the `TFT_eSPI` library's `User_Setup.h` file based on your specific display module).*

## 🚀 Installation & Setup (PlatformIO)

1. **Clone the repository:**
   ```bash
   git clone [https://github.com/OiiiMartinn/ESP32-Clock-with-3.5-TFT-Uno-Module.git](https://github.com/OiiiMartinn/ESP32-Clock-with-3.5-TFT-Uno-Module.git)