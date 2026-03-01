#include <Arduino.h>
#include <SPI.h>
#include <WiFiNINA.h>

#include "secrets.h"
#include "config.h"
#include "hardware.h"
#include "ntp.h"
#include "schedule.h"
#include "webserver.h"

// --- Hardware Instances ---
Valve insideValve(PIN_VALVE_INSIDE);
Valve outsideValve(PIN_VALVE_OUTSIDE);
Valve tankValve(PIN_VALVE_TANK);

Switch insideSwitch(PIN_SW_INSIDE);
Switch outsideSwitch(PIN_SW_OUTSIDE);
Switch insideAutoSwitch(PIN_SW_INSIDE_AUTO);
Switch outsideAutoSwitch(PIN_SW_OUTSIDE_AUTO);

// --- Pump State ---
bool pumpActive = false;
unsigned long valveActivationTime = 0;
bool waitingForPump = false;

void setup() {
  Serial.begin(115200);

  // Hardware init
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);
  insideValve.begin();  outsideValve.begin();  tankValve.begin();
  insideSwitch.begin(); outsideSwitch.begin();
  insideAutoSwitch.begin(); outsideAutoSwitch.begin();

  loadSchedules();
  loadValvePwm();
  insideValve.setPwmOn(valvePwm[0]);   outsideValve.setPwmOn(valvePwm[1]);   tankValve.setPwmOn(valvePwm[2]);
  insideValve.setPwmHold(valvePwm[3]); outsideValve.setPwmHold(valvePwm[4]); tankValve.setPwmHold(valvePwm[5]);

  // Connect WiFi
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }

  IPAddress ip(192, 168, 1, 200);
  IPAddress dns(8, 8, 8, 8);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(ip, dns, gateway, subnet);

  Serial.print("Attempting to connect to SSID: ");
  Serial.println(WIFI_SSID);
  int status = WL_IDLE_STATUS;
  do {
    status = WiFi.begin(WIFI_SSID, WIFI_PASS);
    delay(5000);
  } while (status != WL_CONNECTED);

  printWiFiStatus();

  Serial.print("Fetching NTP time via UDP...");
  unsigned long t = fetchNTPTime();
  if (t != 0) {
    Serial.println("\nTime Synchronized!");
    currentEpoch = t + TIMEZONE_OFFSET_SEC;
  } else {
    Serial.println("\nTime Sync Timeout - Will retry in background.");
    currentEpoch = 0;
  }
  lastEpochUpdateMillis = millis();

  webserverBegin();
}

void loop() {
  manageSchedules();
  handleClient();

  bool reqInside   = insideSwitch.isOn();
  bool reqOutside  = outsideSwitch.isOn();
  bool autoInside  = insideAutoSwitch.isOn();  // Always polled to keep debounce state current
  bool autoOutside = outsideAutoSwitch.isOn();

  if (scheduleRunning) {
    unsigned long elapsedMillis = millis() - scheduleStartTimeMillis;
    unsigned long currentPhaseMinutes = elapsedMillis / 60000UL;
    bool evenPhase = (currentPhaseMinutes / 5) % 2 == 0;

    if (evenPhase) {
      reqInside = autoInside; reqOutside = false;        // Phase 1, 3, 5... = Inside (if enabled)
    } else {
      reqInside = false;      reqOutside = autoOutside;  // Phase 2, 4, 6... = Outside (if enabled)
    }
  }

  bool anyDemanded = reqInside || reqOutside;

  if (!anyDemanded) {
    if (pumpActive || waitingForPump || insideValve.isActive() || outsideValve.isActive() || tankValve.isActive()) {
      pumpActive = false;
      digitalWrite(PIN_PUMP, LOW);
      delay(50);
      insideValve.turnOff();
      outsideValve.turnOff();
      tankValve.turnOff();
      waitingForPump = false;
    }
  } else {
    bool newlyActivated = false;

    if (!tankValve.isActive()) {
      tankValve.turnOn();
      newlyActivated = true;
    }

    if (reqInside && !insideValve.isActive()) {
      insideValve.turnOn();
      newlyActivated = true;
    } else if (!reqInside && insideValve.isActive()) {
      insideValve.turnOff();
    }

    if (reqOutside && !outsideValve.isActive()) {
      outsideValve.turnOn();
      newlyActivated = true;
    } else if (!reqOutside && outsideValve.isActive()) {
      outsideValve.turnOff();
    }

    if (newlyActivated && !pumpActive && !waitingForPump) {
      waitingForPump = true;
      valveActivationTime = millis();
    }

    if (waitingForPump && millis() - valveActivationTime >= PUMP_START_DELAY_MS) {
      pumpActive = true;
      digitalWrite(PIN_PUMP, HIGH);
      waitingForPump = false;
    }
  }

  insideValve.update();
  outsideValve.update();
  tankValve.update();
}
