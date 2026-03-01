#pragma once

#include <Arduino.h>
#include "config.h"

class Valve {
  int pin;
  bool active;
  bool holding;
  unsigned long activeStartTime;

public:
  Valve(int p) : pin(p), active(false), holding(false), activeStartTime(0) {}

  void begin() {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  void turnOn() {
    if (!active) {
      active = true;
      holding = false;
      activeStartTime = millis();
      analogWrite(pin, PWM_100);
    }
  }

  void turnOff() {
    if (active) {
      active = false;
      analogWrite(pin, 0);    // Force PWM to 0V
      digitalWrite(pin, LOW); // Extra safety
    }
  }

  void update() {
    if (active && !holding) {
      if (millis() - activeStartTime >= PWM_100_DURATION_MS) {
        holding = true;
        analogWrite(pin, PWM_50);
      }
    }
  }

  bool isActive() const { return active; }
};

class Switch {
  int pin;
  bool state;
  bool lastReading;
  unsigned long lastDebounceTime;
  unsigned long debounceDelay;

public:
  Switch(int p, unsigned long delayMs = 50)
    : pin(p), state(false), lastReading(false), lastDebounceTime(0), debounceDelay(delayMs) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    state = (digitalRead(pin) == LOW);
    lastReading = state;
  }

  bool isOn() {
    bool reading = (digitalRead(pin) == LOW);
    if (reading != lastReading) lastDebounceTime = millis();
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != state) state = reading;
    }
    lastReading = reading;
    return state;
  }
};
