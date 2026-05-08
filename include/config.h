#pragma once

#define FIRMWARE_VERSION "1.3.3"

// --- Hardware Pins (ESP32-S3-WROOM-1) ---
const int PIN_PUMP            =  4;
const int PIN_VALVE_INSIDE    =  6;
const int PIN_VALVE_OUTSIDE   =  5;
const int PIN_VALVE_TANK      =  7;
const int PIN_SW_INSIDE_AUTO  =  8; // Must be ON for schedule to water inside
const int PIN_SW_INSIDE       =  9;
const int PIN_SW_OUTSIDE      = 10;
const int PIN_SW_OUTSIDE_AUTO = 11; // Must be ON for schedule to water outside
// I2C EEPROM (AT24Cxx)
const int PIN_I2C_SDA = 21;
const int PIN_I2C_SCL = 2;

// --- Timing Constants ---
const unsigned long PWM_100_DURATION_MS = 2000;
const unsigned long PUMP_START_DELAY_MS = 1000;
const int PWM_100 = 255;
const int PWM_50  = 127;

// --- Scheduling Constants ---
const int MAX_SCHEDULES = 5;
const long TIMEZONE_OFFSET_SEC = -21600; // UTC-6 for Merida standard time (no DST)
const unsigned long VALVE_OVERLAP_MS = 5000; // Both valves open during phase transitions
