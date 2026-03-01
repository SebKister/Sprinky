#pragma once

#include <Arduino.h>
#include "config.h"

struct Schedule {
  int startHour;
  int startMinute;
  int durationMinutes;
  bool active;
};

extern Schedule schedules[MAX_SCHEDULES];
extern bool scheduleRunning;
extern unsigned long scheduleStartTimeMillis;
extern int currentScheduleIndex;

void loadSchedules();
void saveSchedules();
void manageSchedules();
