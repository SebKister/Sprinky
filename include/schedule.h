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

// [0-2] = on% for inside/outside/tank, [3-5] = hold% for inside/outside/tank
extern int valvePwm[6];
void loadValvePwm();
void saveValvePwm();
