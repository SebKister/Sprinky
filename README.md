# Sprinky

Firmware for a sprinkler control system running on the YD-ESP32-S3 (2022-v1.3).

## Hardware Setup

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
| 21   | I2C SDA (EEPROM)       |
| 2    | I2C SCL (EEPROM)       |

### Pin diagram

```text
                       YD-ESP32-S3 (2022-v1.3)
                      ┌────────────────────────┐
                GND ──┤○   [COM]    [USB]      ○├── 5Vin
                GND ──┤○                      ○├── GND
                 20 ──┤○                      ○├── 13
   IO21 · EEPROM SDA ──┤○                      ○├── 12
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
    IO2 · EEPROM SCL ──┤○                      ○├── IO5  · Inside valve
                  1 ──┤○                      ○├── IO4  · Pump relay
         RX (IO44) ──┤○                      ○├── RST
         TX (IO43) ──┤○                      ○├── 3V3
                GND ──┤○                      ○├── 3V3
                      └────────────────────────┘
```

> Avoid GPIO 0 (boot strapping), GPIO 19/20 (USB OTG), GPIO 46 (strapping pin), and GPIO 26–32 (flash/PSRAM).

### I2C EEPROM wiring

EEPROM: **Microchip 24AA512** (512 Kbit / 64 KB, 128-byte pages, 400 kHz Fast mode).

| EEPROM pin | Connect to          | Notes                          |
|------------|---------------------|--------------------------------|
| VCC        | 3.3 V               |                                |
| GND        | GND                 |                                |
| SDA        | GPIO 21             | 4.7 kΩ pull-up to 3.3 V        |
| SCL        | GPIO 2              | 4.7 kΩ pull-up to 3.3 V        |
| A0, A1, A2 | GND                 | I2C address → 0x50             |
| WP         | GND                 | Write-protect disabled         |

## Project Structure

```text
include/
  config.h          — Pin assignments, I2C pins, and timing/scheduling constants
  hardware.h        — Valve and Switch classes
  ntp.h             — NTP time state (currentEpoch, lastEpochUpdateMillis) and fetchNTPTime() declaration
  schedule.h        — Schedule struct, schedule state globals, and function declarations
  eeprom_storage.h  — I2C EEPROM load/save API
  webserver.h       — Web server function declarations
src/esp32s3/
  eeprom_storage.cpp — Wire-based I2C EEPROM driver (AT24Cxx page-write + sequential read)
  schedule.cpp      — Delegates load/save to eeprom_storage and runs manageSchedules()
  webserver.cpp     — HTTP server and HTML page rendering
  main.cpp          — setup(), loop(), hardware instances, pump control logic
  ntp.cpp           — NTP time sync over UDP
```

## Logic Overview

### Startup sequence

1. Initialise all output pins LOW (pump and valves off)
2. Initialise I2C and load saved schedules and valve PWM settings from external EEPROM
3. Connect to WiFi with a static IP (`192.168.1.202`)
4. Fetch the current time from `pool.ntp.org` via UDP and apply a UTC-6 offset
5. Start the HTTP server on port 80

### Manual watering

**Physical switches:**

**Inside switch (pin 9)**: held ON → waters inside zone
**Outside switch (pin 10)**: held ON → waters outside zone

Manual switches work at any time and are independent of schedules or auto switches.

**Web interface toggles:**

The web page provides Inside and Outside toggle buttons under **Manual Zone Control**. These work alongside the physical switches — either can activate a zone independently.

### Scheduled watering

Up to 5 schedules can be configured via the web page. Each schedule has:

**Active** checkbox — enables or disables the schedule
**Start time** — hour and minute (UTC-6) at which the schedule triggers
**Duration** — total watering time in minutes

When a schedule starts it alternates between zones in 5-minute phases:

| Phase              | Zone watered                                         |
|--------------------|------------------------------------------------------|
| 1, 3, 5 ... (even) | Inside - only if inside auto switch (pin 8) is ON    |
| 2, 4, 6 ... (odd)  | Outside - only if outside auto switch (pin 11) is ON |

The auto switches act as physical enable/disable gates per zone. If an auto switch is OFF, the schedule skips that zone's phases entirely (no water flows for that zone that session).

Schedules are saved to the external I2C EEPROM and survive power cycles and firmware reflashes.

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

Both percentages are configurable per valve from the web interface and saved to EEPROM.

### EEPROM storage layout

Data is stored in the external AT24Cxx EEPROM starting at address `0x0000`:

| Address | Size      | Contents                                              |
|---------|-----------|-------------------------------------------------------|
| 0x0000  | 2 bytes   | Magic marker `0xAB 0xCD` (validates written data)     |
| 0x0002  | ~65 bytes | `schedules[5]` - 5x Schedule struct                   |
| ~0x0043 | 24 bytes  | `valvePwm[6]` - 6x int (on/hold % per valve)          |

If the magic marker is absent on first boot, all schedules default to inactive and valve PWM defaults to 100 % on / 50 % hold.

### Time synchronisation

* Initial sync at boot via UDP NTP (5 attempts, 2-second timeout each)
* Re-syncs every hour while running
* If offline at boot, retries every 10 seconds until a response is received
* Between syncs, time is estimated by extrapolating from `millis()`

### Web interface

Accessible at `http://192.168.1.202` on the local network. Provides:

* **Manual Zone Control** — toggle buttons to turn inside/outside zones on or off from the browser
* **Status table** — live state of pump, all three valves, all four switches, and active schedule
* **Schedules** — view and edit up to 5 schedules (active flag, start time, duration); saved to EEPROM on submit
* **Valve PWM %** — set On % and Hold % independently for each valve; applied immediately and saved to EEPROM
