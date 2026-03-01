#pragma once

#include <Arduino.h>

extern unsigned long currentEpoch;
extern unsigned long lastEpochUpdateMillis;

unsigned long fetchNTPTime();
