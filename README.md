# Sprinky

Firmware for a sprinkler control system supporting two targets:

| PlatformIO env      | Board                        |
|---------------------|------------------------------|
| `nanorp2040connect` | Arduino Nano RP2040 Connect  |
| `esp32s3`           | YD-ESP32-S3 (2022-v1.3)      |

## Hardware Setup

### Arduino Nano RP2040 Connect

| Pin | Component              |
|-----|------------------------|
| 2   | Pump relay             |
| 3   | Inside valve           |
| 4   | Outside valve          |
| 5   | Tank valve             |
| 14  | Inside auto switch     |
| 15  | Inside manual switch   |
| 16  | Outside manual switch  |
| 17  | Outside auto switch    |

### Pin diagram

```text
                          Arduino Nano RP2040 Connect
                          ┌────────────────────────┐
                    D13 ──┤○                      ○├── D12
                    3V3 ──┤○                      ○├── D11
                   AREF ──┤○                      ○├── D10
    D14 · Inside auto sw ──┤○                      ○├── D9
   D15 · Inside manual sw ──┤○                      ○├── D8
  D16 · Outside manual sw ──┤○                      ○├── D7
   D17 · Outside auto sw ──┤○                      ○├── D6
                  A4/D18 ──┤○                      ○├── D5 · Tank valve
                  A5/D19 ──┤○                      ○├── D4 · Outside valve
                      A6 ──┤○                      ○├── D3 · Inside valve
                      A7 ──┤○                      ○├── D2 · Pump relay
                      5V ──┤○                      ○├── GND
                     RST ──┤○                      ○├── RST
                     GND ──┤○                      ○├── TX
                     VIN ──┤○                      ○├── RX
                          └────────────────────────┘
```

### YD-ESP32-S3 (2022-v1.3)

| GPIO | Component              |
|------|------------------------|
| 4    | Pump relay             |
| 5    | Inside valve           |
| 6    | Outside valve          |
| 7    | Tank valve             |
| 8    | Inside auto switch     |
| 9    | Inside manual switch   |
| 10   | Outside manual switch  |
| 11   | Outside auto switch    |

### ESP32-S3 pin diagram

```text
                       YD-ESP32-S3 (2022-v1.3)
                      ┌────────────────────────┐
                GND ──┤○   [COM]    [USB]      ○├── 5Vin
                GND ──┤○                      ○├── GND
                 20 ──┤○                      ○├── 13
                 21 ──┤○                      ○├── 12
                 47 ──┤○                      ○├── IO11 · Outside auto sw
                 48 ──┤○                      ○├── IO10 · Outside manual sw
                 45 ──┤○   [RGB LED]          ○├── IO9  · Inside manual sw
                  0 ──┤○                      ○├── 46
                 35 ──┤○                      ○├── 3
                 36 ──┤○   [BOOT]             ○├── IO8  · Inside auto sw
                 37 ──┤○                      ○├── 18
                 38 ──┤○   [RST]              ○├── 17
                 39 ──┤○                      ○├── 16
                 40 ──┤○                      ○├── 15
                 41 ──┤○                      ○├── IO7  · Tank valve
                 42 ──┤○                      ○├── IO6  · Outside valve
                  2 ──┤○                      ○├── IO5  · Inside valve
                  1 ──┤○                      ○├── IO4  · Pump relay
         RX (IO44) ──┤○                      ○├── RST
         TX (IO43) ──┤○                      ○├── 3V3
                GND ──┤○                      ○├── 3V3
                      └────────────────────────┘
```

> Avoid GPIO 0 (boot strapping), GPIO 19/20 (USB OTG), GPIO 46 (strapping pin), and GPIO 26–32 (flash/PSRAM).

### ESP32-S3 platform differences

| Concern          | RP2040 (WiFiNINA)             | ESP32-S3                              |
|------------------|-------------------------------|---------------------------------------|
| WiFi library     | `WiFiNINA`                    | Built-in `WiFi.h`                     |
| Flash storage    | `WiFiStorage` (NINA flash)    | `LittleFS` (ESP32 internal flash)     |
| HTTP server      | Manual `WiFiServer` loop      | `WebServer.h` with route handlers     |
| PWM              | `analogWrite()` (RP2040)      | `analogWrite()` (Arduino core 3.x)    |
| `WiFi.config()`  | `(ip, dns, gateway, subnet)`  | `(ip, gateway, subnet, dns)`          |

## Project Structure

```text
include/
  config.h      — Pin assignments and timing/scheduling constants
  hardware.h    — Valve and Switch classes
  ntp.h         — NTP time state (currentEpoch, lastEpochUpdateMillis) and fetchNTPTime() declaration
  schedule.h    — Schedule struct, schedule state globals, and function declarations
  webserver.h   — Web server function declarations
src/
  ntp.cpp       — NTP time sync over UDP (pool.ntp.org); retries every hour, or every 10s if not yet synced
  schedule.cpp  — Schedule load/save (WiFiNINA flash) and manageSchedules()
  webserver.cpp — HTTP server and HTML page rendering
  main.cpp      — setup(), loop(), hardware instances, pump control logic
  secrets.h     — WiFi credentials (not committed)
```

## Logic Overview

### Startup sequence

1. Initialise all output pins LOW (pump and valves off)
2. Load saved schedules from the WiFiNINA module's onboard flash
3. Connect to WiFi with a static IP (`192.168.1.200` on RP2040, `192.168.1.202` on ESP32-S3)
4. Fetch the current time from `pool.ntp.org` via UDP and apply a UTC-6 offset
5. Start the HTTP server on port 80

### Manual watering (physical switches)

***Inside switch (pin 15)**: held ON → waters inside zone
***Outside switch (pin 16)**: held ON → waters outside zone

Manual switches work at any time and are independent of schedules or auto switches.

### Scheduled watering

Up to 5 schedules can be configured via the web page. Each schedule has:

***Active** checkbox — enables or disables the schedule
***Start time** — hour and minute (UTC-6) at which the schedule triggers
***Duration** — total watering time in minutes

When a schedule starts it alternates between zones in 5-minute phases:

| Phase              | Zone watered                                         |
|--------------------|------------------------------------------------------|
| 1, 3, 5 ... (even) | Inside - only if inside auto switch (pin 14) is ON   |
| 2, 4, 6 ... (odd)  | Outside - only if outside auto switch (pin 17) is ON |

The auto switches act as physical enable/disable gates per zone. If an auto switch is OFF, the schedule skips that zone's phases entirely (no water flows for that zone that session).

Schedules are saved to the WiFiNINA module's onboard flash and survive power cycles.

### Valve and pump sequencing

To protect the pump, the following sequence is enforced on every state change:

**Turning ON:**

1. Open tank valve (and the relevant zone valve)
2. Wait 1 second (`PUMP_START_DELAY_MS`)
3. Start pump

**Turning OFF:**

1. Stop pump immediately
2. Wait 50 ms
3. Close all valves

### Phase transition overlap

At each 5-minute phase boundary, both zone valves remain open simultaneously for `VALVE_OVERLAP_MS` (default 5 seconds) before the outgoing valve closes. This prevents a pressure drop during the switch and results in a smoother transition. The pump and tank valve stay on continuously throughout.

### Valve PWM

Each valve has two independently configurable PWM levels:

| Phase    | Default | Purpose                                                                          |
|----------|---------|----------------------------------------------------------------------------------|
| **On**   | 100 %   | Full power for the first 2 s (`PWM_100_DURATION_MS`) to ensure reliable opening  |
| **Hold** | 50 %    | Reduced power after opening to limit heat and current draw                       |

Both percentages are configurable per valve from the web interface and saved to flash.

### Time synchronisation

* Initial sync at boot via UDP NTP (5 attempts, 2-second timeout each)
* Re-syncs every hour while running
* If offline at boot, retries every 10 seconds until a response is received
* Between syncs, time is estimated by extrapolating from `millis()`

### Web interface

Accessible at `http://192.168.1.200` (RP2040) or `http://192.168.1.202` (ESP32-S3) on the local network. Provides:

* **Status table** — live state of pump, all three valves, all four switches, and active schedule
* **Schedules** — view and edit up to 5 schedules (active flag, start time, duration); saved to flash on submit
* **Valve PWM %** — set On % and Hold % independently for each valve; applied immediately and saved to flash
