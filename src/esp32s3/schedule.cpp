#include "schedule.h"
#include "eeprom_storage.h"
#include "ntp.h"
#include "config.h"

Schedule schedules[MAX_SCHEDULES];
bool scheduleRunning = false;
unsigned long scheduleStartTimeMillis = 0;
int currentScheduleIndex = -1;

// When a schedule is manually stopped, suppress auto-restart until the
// minute-of-day rolls past this value. -1 means no suppression.
static int suppressMinuteOfDay = -1;

// [0-2] = on% for inside/outside/tank, [3-5] = hold% for inside/outside/tank
int valvePwm[6] = {100, 100, 100, 50, 50, 50};

int insidePhaseMins  = 5;
int outsidePhaseMins = 5;

void loadSchedules() {
  eepromLoadSchedules();
}

void saveSchedules() {
  eepromSaveSchedules();
}

void loadValvePwm() {
  eepromLoadValvePwm();
}

void saveValvePwm() {
  eepromSaveValvePwm();
}

void loadPhaseMins() {
  eepromLoadPhaseMins();
}

void savePhaseMins() {
  eepromSavePhaseMins();
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
      lastEpochUpdateMillis = now;
    }
  }

  unsigned long estimatedEpoch = currentEpoch + ((now - lastEpochUpdateMillis) / 1000);
  int ch = (estimatedEpoch % 86400L) / 3600;
  int cm = (estimatedEpoch % 3600) / 60;
  int mod = ch * 60 + cm;

  // Clear suppression once we move past the minute the user stopped in
  if (suppressMinuteOfDay >= 0 && mod != suppressMinuteOfDay) {
    suppressMinuteOfDay = -1;
  }

  if (!scheduleRunning) {
    if (mod == suppressMinuteOfDay) return;
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

void stopSchedule() {
  if (!scheduleRunning) return;
  unsigned long estimatedEpoch = currentEpoch + ((millis() - lastEpochUpdateMillis) / 1000);
  int ch = (estimatedEpoch % 86400L) / 3600;
  int cm = (estimatedEpoch % 3600) / 60;
  suppressMinuteOfDay = ch * 60 + cm;
  scheduleRunning = false;
  currentScheduleIndex = -1;
  Serial.println("Schedule stopped manually.");
}
