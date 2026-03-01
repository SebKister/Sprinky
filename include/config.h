#pragma once

// --- Hardware Pins ---
const int PIN_PUMP          = 2;
const int PIN_VALVE_INSIDE  = 3;
const int PIN_VALVE_OUTSIDE = 4;
const int PIN_VALVE_TANK    = 5;
const int PIN_SW_INSIDE     = 15;
const int PIN_SW_OUTSIDE    = 16;
const int PIN_SW_INSIDE_AUTO  = 14; // Must be ON for schedule to water inside
const int PIN_SW_OUTSIDE_AUTO = 17; // Must be ON for schedule to water outside

// --- Timing Constants ---
const unsigned long PWM_100_DURATION_MS = 2000;
const unsigned long PUMP_START_DELAY_MS = 1000;
const int PWM_100 = 255;
const int PWM_50  = 127;

// --- Scheduling Constants ---
const int MAX_SCHEDULES = 5;
const long TIMEZONE_OFFSET_SEC = -21600; // UTC-6 for Merida standard time (no DST)
