# Wisper
# Wisper: ESP8266-Powered Task Manager & Pomodoro Timer

**Wisper** is a self-hosted, distraction-free desktop companion built on the ESP8266 microcontroller. It provides:

- A browser-based task manager for adding, editing, and removing tasks.
- A customizable Pomodoro timer with visual and LED alerts.
- Persistent storage of all settings and tasks across power cycles.
- A playful OLED animation mode for brief moments of delight.

---

## Table of Contents
1. [Project Overview](#project-overview)
2. [Features](#features)
3. [Hardware Components](#hardware-components)
4. [Schematics](#Schematics).
5. [Software Dependencies](#software-dependencies)
6. [Wiring & Connections](#wiring--connections)
7. [Setup Instructions](#setup-instructions)
8. [Usage](#usage)
9. [Code Structure](#code-structure)
10. [Persistence & Data Storage](#persistence--data-storage)
11. [Animation Mode Details](#animation-mode-details)
12. [Customization & Configuration](#customization--configuration)
13. [Troubleshooting](#troubleshooting)
14. [Future Improvements](#future-improvements)
15. [License](#license)

---

## Project Overview

Wisper transforms a simple ESP8266 and SH1106 OLED into a powerful desktop assistant. By hosting a local web server, it lets you manage tasks and run Pomodoro sessions through any browser—while displaying status, countdowns, and fun animations on the desk-mounted OLED.

## Features

1. **Browser-Based Task Manager**  
   - Add up to 20 tasks with a title, description, start time, end time, and priority.  
   - Edit or clear the entire list remotely.  
   - Tasks are displayed on the OLED in normal or large-time format.

2. **Customizable Pomodoro Timer**  
   - Configure work duration (5–180 minutes), break duration (2–30 minutes), and number of sets (1–10) from the web UI.  
   - Real-time countdown on OLED.  
   - LED blink alerts at session transitions.  
   - Pause, resume, and stop controls via web or hardware button.

3. **State Persistence**  
   - All tasks and Pomodoro settings saved to `SPIFFS` (`/data.json`) using ArduinoJson.  
   - Automatic load on boot, ensuring no data loss after power cycles.

4. **NTP-Synchronized Real-Time Clock**  
   - Syncs every minute with `pool.ntp.org` (UTC+5:30).  
   - Accurate scheduling and display of current time.  
   - Hourly LED blink reminder.

5. **Alternate “Eye” Animation Demo**  
   - Eight non-blocking animations: wakeup, blink, saccade, happy eye, sleep, random gaze, and more.  
   - Toggle mode with a dedicated hardware button for a quick mood boost.

6. **Power-Saving & Inactivity**  
   - OLED screen auto-off after 3 minutes of inactivity.  
   - Screen brightness control via contrast settings.

## Hardware Components

- **MCU:** ESP8266 (NodeMCU or similar)
- **Display:** SH1106 128×64 OLED (I2C Interface)
- **Buttons:**  
  - D5 (GPIO14): Toggle between large and normal time display.  
  - D6 (GPIO12): Enter/exit Pomodoro mode on OLED.  
  - D7 (GPIO13): Toggle animation demo mode.
- **LED:** GPIO15 for visual blink alerts.
- **Power:** 5V via USB cable.
- 
- ## Schematics

Below are the schematic images and prototype photos for Wisper.

> **Note:** I don't have mowby to buy a PCB currently, so I initially built the circuit on a prototype board ("zero PCB") for testing. The images below show the hand-built prototype and the schematic I used to lay out the final PCB.

### Schematic images

*Figure 1 — Main circuit schematic (ESP8266, SH1106, buttons, LED).*
<img width="1165" height="795" alt="Screenshot 2025-11-20 104217" src="https://github.com/user-attachments/assets/66d0405f-bdd8-4278-bac8-4e17771b887e" />

*Figure 2 — Close-up of power and button wiring.*
<img width="549" height="671" alt="Screenshot 2025-11-20 103331" src="https://github.com/user-attachments/assets/18776ab6-0b6c-4780-855f-13ad1b6bad2c" />


<img width="549" height="671" alt="Screenshot 2025-11-20 103331" src="https://github.com/user-attachments/assets/a7c6078d-b9a4-4147-a845-472d1de2b919" />
)  
*Figure 3 — 3d model of pcb *
<img width="1225" height="736" alt="Screenshot 2025-11-20 103347" src="https://github.com/user-attachments/assets/20fdc391-dc9e-43d3-90c8-b197e206f7d4" />


*Figure 4- bottom side of 3d model of pcb *
<img width="1232" height="722" alt="Screenshot 2025-11-20 103358" src="https://github.com/user-attachments/assets/ae541ee2-1233-4a0f-ab38-0863c204d026" />


## Software Dependencies

This project uses the following Arduino libraries:

- `ESP8266WiFi` — for connecting to Wi-Fi.
- `ESP8266WebServer` — for hosting HTTP endpoints.
- `WiFiUdp` & `NTPClient` — for NTP time synchronization.
- `ArduinoJson` — for serializing and deserializing state.
- `SH1106Wire` — for driving the SH1106 OLED display.
- `FS` (SPIFFS) — for file storage on flash.

Install these via the Arduino Library Manager before uploading.

## Wiring & Connections

| Component        | ESP8266 Pin | Notes                             |
|------------------|-------------|-----------------------------------|
| OLED SDA (Wire)  | GPIO4 (D2)  | Connect to SH1106 SDA             |
| OLED SCL (Wire)  | GPIO5 (D1)  | Connect to SH1106 SCL             |
| Button 1 (Mode)  | GPIO14 (D5) | Input pull-up, toggles time view  |
| Button 2 (Pomodoro) | GPIO12 (D6) | Input pull-up, toggles Pomodoro UI|
| Button 3 (Animate) | GPIO13 (D7) | Input pull-up, toggles animations |
| LED              | GPIO15 (D8) | Output for blink alerts           |

## Setup Instructions

1. Clone the repository:
   ```bash
   git clone https://github.com/rudrajadaun1/wisper.git
   cd wisper
   ```
2. Open `wisper.ino` in Arduino IDE.
3. Install required libraries via **Sketch → Include Library → Manage Libraries**.
4. Update your Wi-Fi credentials in the code:
   ```cpp
   const char* wifiSSID = "your-ssid";
   const char* wifiPassword = "your-password";
   ```
5. Upload to your ESP8266 board.
6. Open Serial Monitor at 115200 baud to view the IP address.
7. Navigate to the printed IP in your browser to access the Wisper web interface.

## Usage

- **Task Management:** Use the web form to add tasks. Click “Clear Timetable” to remove all tasks.  
- **Pomodoro Controls:** On the web UI, set work/music durations and click “Start Pomodoro”. Use “Pause/Resume” and “Stop Pomodoro” buttons as needed.  
- **Hardware Buttons:** D5 toggles time display size; D6 toggles Pomodoro mode on the OLED; D7 toggles animation mode.  
- **Screen Auto-Off:** Screen dims after 3 minutes without button presses. Press any mode button to wake the screen.

## Code Structure

- `wisper.ino` — Main sketch file containing setup, loop, and all functions.
- **Sections in code:**
  - Global variable definitions (task array, Pomodoro state, UI flags)
  - Helper functions (`parseTime()`, `formatSecondsToHMS()`, `blinkLED()`, etc.)
  - Persistence functions (`saveState()`, `loadState()`)
  - Web server handlers (`handleRoot`, `handleUpload`, ...)
  - UI draw functions (`drawNormalScreen()`, `drawPomodoroScreen()`, `launch_animation_with_index()`)
  - `setup()` and `loop()` implementations.

## Persistence & Data Storage

- Uses `SPIFFS` to store `data.json` in flash.
- Tasks and Pomodoro settings are serialized to JSON on every change.
- On startup, Wisper loads `data.json` to restore the last session.

## Animation Mode Details

- Eight animations implemented in `wisper.ino`:
  1. **Wakeup**: Gradual eye opening.
  2. **Center Eyes**: Reset to neutral.
  3. **Big Eye Movement**: Widen one eye.
  4. **Big Eye Movement (other)**: Widen the opposite eye.
  5. **Blink Fast**: Quick double blink.
  6. **Blink Slow**: Slow, exaggerated blink.
  7. **Happy Eye**: Curved eyelid lines.
  8. **Sleep**: Minimal slits.
  9. **Random Gaze**: Small saccades in random directions.

Animations run non-blocking in `loop()` and cycle every 500 ms. Toggle with the third button (D7).

## Customization & Configuration

- Adjust Pomodoro defaults at the top of the code:
  ```cpp
  int pomodoroWorkMinutes = 25;
  int pomodoroBreakMinutes = 5;
  int pomodoroSets = 4;
  ```
- Change inactivity timeout (`180000` ms) for screen-off in `loop()`.
- Modify scroll speed and text offsets for task display by editing `scrollDelay`, `displayWidth`, and related variables.

## Troubleshooting

- **OLED blank?** Verify I2C wiring and correct address (0x3C).
- **Web UI unreachable?** Check Serial Monitor for IP and ensure your computer is on the same network.
- **SPIFFS errors?** Format SPIFFS from Arduino IDE or add `SPIFFS.format()` once.

## Future Improvements

- Voice command integration via external microphone.
- Calendar sync (Google Calendar, Outlook).
- Theming support for web UI and OLED.
- Bluetooth connectivity for phone notifications.
- Sound alerts via buzzer.

## License

This project is released under the MIT License. See [LICENSE](LICENSE) for details.
![P1050579](https://github.com/user-attachments/assets/27e69d7a-fba9-43ed-810d-dcddd8e92143)


