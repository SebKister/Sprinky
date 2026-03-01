#include <Arduino.h>

// Pins
const int PIN_PUMP          = 2;
const int PIN_VALVE_INSIDE  = 3;
const int PIN_VALVE_OUTSIDE = 4;
const int PIN_VALVE_TANK    = 5;

const int PIN_SW_INSIDE     = 15;
const int PIN_SW_OUTSIDE    = 16;

// Timing Constants
const unsigned long PWM_100_DURATION_MS = 2000; // Pull duration for 100% PWM
const unsigned long PUMP_START_DELAY_MS = 1000; // Delay before pump starts

// PWM Constants (8-bit resolution default in Arduino)
const int PWM_100 = 255;
const int PWM_50  = 127;

// Helper Class to manage Valves (PWM control)
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
      digitalWrite(pin, LOW); // Turn off PWM
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

  bool isActive() const {
    return active;
  }
};

// Simple debouncer for switches
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
    // We use INPUT_PULLUP and assume LOW means connected (switch ON)
    pinMode(pin, INPUT_PULLUP);
    state = (digitalRead(pin) == LOW);
    lastReading = state;
  }

  bool isOn() {
    bool reading = (digitalRead(pin) == LOW);
    if (reading != lastReading) {
      lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != state) {
        state = reading;
      }
    }
    
    lastReading = reading;
    return state;
  }
};

// Global Objects
Valve insideValve(PIN_VALVE_INSIDE);
Valve outsideValve(PIN_VALVE_OUTSIDE);
Valve tankValve(PIN_VALVE_TANK);

Switch insideSwitch(PIN_SW_INSIDE);
Switch outsideSwitch(PIN_SW_OUTSIDE);

bool pumpActive = false;
unsigned long valveActivationTime = 0;
bool waitingForPump = false;

void setup() {
  Serial.begin(115200);

  // Initialize Pump Relay
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);

  // Initialize Valves
  insideValve.begin();
  outsideValve.begin();
  tankValve.begin();

  // Initialize Switches
  insideSwitch.begin();
  outsideSwitch.begin();
}

void loop() {
  bool insideDemanded = insideSwitch.isOn();
  bool outsideDemanded = outsideSwitch.isOn();

  bool anyDemanded = insideDemanded || outsideDemanded;

  if (!anyDemanded) {
    // If no sprinklers are demanded, ensure everything is OFF
    // To turn off, always turn off pump first than valves (both Tank and sprinkler valve)
    if (pumpActive || insideValve.isActive() || outsideValve.isActive() || tankValve.isActive()) {
      
      // Turn off pump FIRST
      pumpActive = false;
      digitalWrite(PIN_PUMP, LOW);
      
      // Small physical delay to allow relay/pump to disengage before cutting valves
      delay(50);
      
      // Then turn off all valves
      insideValve.turnOff();
      outsideValve.turnOff();
      tankValve.turnOff();
      
      waitingForPump = false;
    }
  } else {
    // At least one sprinkler is demanded
    bool newlyActivated = false;

    // First ensure Tank Valve is ON
    if (!tankValve.isActive()) {
      tankValve.turnOn();
      newlyActivated = true;
    }

    // Handle Inside Valve
    if (insideDemanded && !insideValve.isActive()) {
      insideValve.turnOn();
      newlyActivated = true;
    } else if (!insideDemanded && insideValve.isActive()) {
      insideValve.turnOff();
    }

    // Handle Outside Valve
    if (outsideDemanded && !outsideValve.isActive()) {
      outsideValve.turnOn();
      newlyActivated = true;
    } else if (!outsideDemanded && outsideValve.isActive()) {
      outsideValve.turnOff();
    }

    // Determine if we need to start the pump with a delay
    if (newlyActivated && !pumpActive && !waitingForPump) {
      // Begin waiting for 1 second pump delay
      waitingForPump = true;
      valveActivationTime = millis();
    }

    // Check if pump is ready to be started
    if (waitingForPump) {
      if (millis() - valveActivationTime >= PUMP_START_DELAY_MS) {
        pumpActive = true;
        digitalWrite(PIN_PUMP, HIGH);
        waitingForPump = false;
      }
    }
  }

  // Update valves strictly to maintain their 100%->50% PWM transition logic
  insideValve.update();
  outsideValve.update();
  tankValve.update();
}