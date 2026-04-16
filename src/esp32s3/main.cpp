#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

#include "secrets.h"
#include "config.h"
#include "hardware.h"
#include "ntp.h"
#include "schedule.h"
#include "eeprom_storage.h"
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

// --- Web Toggle State ---
bool webReqInside  = false;
bool webReqOutside = false;
unsigned long valveActivationTime = 0;
bool waitingForPump = false;

// --- SSE push throttle ---
static unsigned long lastSsePushMillis = 0;
static const unsigned long SSE_PUSH_INTERVAL_MS = 500;

void setup() {
  Serial.begin(115200);

  // Hardware init
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);
  insideValve.begin();  outsideValve.begin();  tankValve.begin();
  insideSwitch.begin(); outsideSwitch.begin();
  insideAutoSwitch.begin(); outsideAutoSwitch.begin();

  // I2C EEPROM init
  eepromInit();

  loadSchedules();
  loadValvePwm();
  loadPhaseMins();
  insideValve.setPwmOn(valvePwm[0]);   outsideValve.setPwmOn(valvePwm[1]);   tankValve.setPwmOn(valvePwm[2]);
  insideValve.setPwmHold(valvePwm[3]); outsideValve.setPwmHold(valvePwm[4]); tankValve.setPwmHold(valvePwm[5]);

  // Connect WiFi with static IP
  // Note: ESP32 WiFi.config() order differs from WiFiNINA — (ip, gateway, subnet, dns)
  IPAddress ip(192, 168, 1, 202);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(8, 8, 8, 8);
  WiFi.config(ip, gateway, subnet, dns);

  Serial.print("Attempting to connect to SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

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

  // ArduinoOTA for PlatformIO network uploads
  ArduinoOTA.setHostname("sprinky");
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update complete.");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error [%u]\n", error);
  });
  ArduinoOTA.begin();

  webserverBegin();
}

void loop() {
  ArduinoOTA.handle();
  if (otaInProgress) return;
  manageSchedules();

  unsigned long now = millis();
  if (now - lastSsePushMillis >= SSE_PUSH_INTERVAL_MS) {
    webserverPushStatus();
    lastSsePushMillis = now;
  }

  bool reqInside   = insideSwitch.isOn()  || webReqInside;
  bool reqOutside  = outsideSwitch.isOn() || webReqOutside;
  bool autoInside  = insideAutoSwitch.isOn();
  bool autoOutside = outsideAutoSwitch.isOn();

  if (scheduleRunning) {
    unsigned long elapsedMillis     = millis() - scheduleStartTimeMillis;
    unsigned long insideDurationMs  = (unsigned long)insidePhaseMins  * 60000UL;
    unsigned long outsideDurationMs = (unsigned long)outsidePhaseMins * 60000UL;
    unsigned long cycleDurationMs   = insideDurationMs + outsideDurationMs;
    unsigned long cycleElapsedMs    = elapsedMillis % cycleDurationMs;
    bool insidePhase     = cycleElapsedMs < insideDurationMs;
    unsigned long phaseElapsedMs = insidePhase ? cycleElapsedMs : (cycleElapsedMs - insideDurationMs);
    bool inOverlap = phaseElapsedMs < VALVE_OVERLAP_MS;

    if (insidePhase) {
      reqInside  = autoInside;
      reqOutside = inOverlap ? autoOutside : false;
    } else {
      reqOutside = autoOutside;
      reqInside  = inOverlap ? autoInside  : false;
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
