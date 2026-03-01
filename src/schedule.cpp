#include <WiFiStorage.h>
#include "schedule.h"
#include "ntp.h"
#include "config.h"

static const char SCHEDULE_FILENAME[]  = "/schedules.dat";
static const char VALVEPWM_FILENAME[]  = "/valvepwm.dat";

Schedule schedules[MAX_SCHEDULES];
bool scheduleRunning = false;
unsigned long scheduleStartTimeMillis = 0;
int currentScheduleIndex = -1;

// [0-2] = on% for inside/outside/tank, [3-5] = hold% for inside/outside/tank
int valvePwm[6] = {100, 100, 100, 50, 50, 50};

void loadSchedules() {
  if (WiFiStorage.exists(SCHEDULE_FILENAME)) {
    WiFiStorageFile file = WiFiStorage.open(SCHEDULE_FILENAME);
    if (file) {
      file.read((uint8_t*)schedules, sizeof(Schedule) * MAX_SCHEDULES);
      file.close();
      for (int i = 0; i < MAX_SCHEDULES; i++) {
        if (schedules[i].startHour < 0 || schedules[i].startHour > 23 || schedules[i].durationMinutes < 0) {
          schedules[i] = {0, 0, 0, false};
        }
      }
    }
  } else {
    for (int i = 0; i < MAX_SCHEDULES; i++) schedules[i] = {0, 0, 0, false};
  }
}

void saveSchedules() {
  WiFiStorageFile file = WiFiStorage.open(SCHEDULE_FILENAME);
  if (file) {
    file.erase();
    file.write((uint8_t*)schedules, sizeof(Schedule) * MAX_SCHEDULES);
    file.close();
  }
}

void loadValvePwm() {
  if (WiFiStorage.exists(VALVEPWM_FILENAME)) {
    WiFiStorageFile file = WiFiStorage.open(VALVEPWM_FILENAME);
    if (file) {
      file.read((uint8_t*)valvePwm, sizeof(valvePwm));
      file.close();
      for (int i = 0; i < 6; i++) {
        int def = (i < 3) ? 100 : 50;
        if (valvePwm[i] < 0 || valvePwm[i] > 100) valvePwm[i] = def;
      }
    }
  }
}

void saveValvePwm() {
  WiFiStorageFile file = WiFiStorage.open(VALVEPWM_FILENAME);
  if (file) {
    file.erase();
    file.write((uint8_t*)valvePwm, sizeof(valvePwm));
    file.close();
  }
}

void manageSchedules() {
  unsigned long now = millis();

  // Refresh NTP every hour, or retry every 10s if time not yet set
  if (now - lastEpochUpdateMillis > 3600000 || (currentEpoch == 0 && now - lastEpochUpdateMillis > 10000)) {
    unsigned long t = fetchNTPTime();
    if (t > 100000) {
      currentEpoch = t + TIMEZONE_OFFSET_SEC;
      lastEpochUpdateMillis = now;
    } else if (currentEpoch == 0) {
      lastEpochUpdateMillis = now; // Prevent spamming if offline
    }
  }

  unsigned long estimatedEpoch = currentEpoch + ((now - lastEpochUpdateMillis) / 1000);
  int ch = (estimatedEpoch % 86400L) / 3600;
  int cm = (estimatedEpoch % 3600) / 60;

  if (!scheduleRunning) {
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      if (schedules[i].active && schedules[i].startHour == ch && schedules[i].startMinute == cm) {
        scheduleRunning = true;
        currentScheduleIndex = i;
        scheduleStartTimeMillis = millis();
        Serial.print("Auto Schedule Started: ");
        Serial.println(i);
        break;
      }
    }
  } else {
    unsigned long elapsedMillis = millis() - scheduleStartTimeMillis;
    unsigned long targetDurationMillis = (unsigned long)schedules[currentScheduleIndex].durationMinutes * 60000UL;
    if (elapsedMillis >= targetDurationMillis) {
      scheduleRunning = false;
      currentScheduleIndex = -1;
      Serial.println("Auto Schedule Finished.");
    }
  }
}
