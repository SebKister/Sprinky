#include <Wire.h>
#include "eeprom_storage.h"
#include "schedule.h"
#include "config.h"

// Microchip 24AA512 I2C EEPROM (address pins A2-A0 all tied low)
#define EEPROM_I2C_ADDR    0x50
#define EEPROM_PAGE_SIZE   128   // bytes — 24AA512 page size
#define EEPROM_WRITE_MS    5     // max write cycle time (ms)

// EEPROM memory map
#define ADDR_MAGIC         0x0000  // 2 bytes: validity marker
#define ADDR_SCHEDULES     0x0002  // sizeof(Schedule) * MAX_SCHEDULES bytes
#define ADDR_VALVEPWM      (ADDR_SCHEDULES + sizeof(Schedule) * MAX_SCHEDULES)
#define ADDR_PHASE_MINS    (ADDR_VALVEPWM + sizeof(valvePwm))  // 2 ints: insidePhaseMins, outsidePhaseMins

static const uint8_t MAGIC_BYTES[2] = {0xAB, 0xCD};

// ---- low-level helpers --------------------------------------------------

static void eepromWrite(uint16_t addr, const uint8_t* data, size_t len) {
  size_t done = 0;
  while (done < len) {
    uint16_t cur = addr + done;
    // number of bytes remaining until end of current page
    uint16_t pageRem = EEPROM_PAGE_SIZE - (cur % EEPROM_PAGE_SIZE);
    size_t chunk = min((size_t)pageRem, len - done);

    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write(cur >> 8);
    Wire.write(cur & 0xFF);
    for (size_t i = 0; i < chunk; i++) Wire.write(data[done + i]);
    Wire.endTransmission();
    delay(EEPROM_WRITE_MS);
    done += chunk;
  }
}

static void eepromRead(uint16_t addr, uint8_t* data, size_t len) {
  Wire.beginTransmission(EEPROM_I2C_ADDR);
  Wire.write(addr >> 8);
  Wire.write(addr & 0xFF);
  Wire.endTransmission(false);          // repeated start — keep bus

  Wire.requestFrom(EEPROM_I2C_ADDR, (int)len);
  for (size_t i = 0; i < len && Wire.available(); i++) {
    data[i] = Wire.read();
  }
}

static bool eepromIsValid() {
  uint8_t magic[2];
  eepromRead(ADDR_MAGIC, magic, 2);
  return magic[0] == MAGIC_BYTES[0] && magic[1] == MAGIC_BYTES[1];
}

// ---- public API ---------------------------------------------------------

void eepromInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
}

void eepromLoadSchedules() {
  if (!eepromIsValid()) {
    for (int i = 0; i < MAX_SCHEDULES; i++) schedules[i] = {0, 0, 0, false};
    Serial.println("EEPROM: no valid data, using defaults.");
    return;
  }
  eepromRead(ADDR_SCHEDULES, (uint8_t*)schedules, sizeof(Schedule) * MAX_SCHEDULES);
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (schedules[i].startHour < 0 || schedules[i].startHour > 23 ||
        schedules[i].durationMinutes < 0) {
      schedules[i] = {0, 0, 0, false};
    }
  }
}

void eepromSaveSchedules() {
  eepromWrite(ADDR_MAGIC,     MAGIC_BYTES, 2);
  eepromWrite(ADDR_SCHEDULES, (uint8_t*)schedules, sizeof(Schedule) * MAX_SCHEDULES);
}

void eepromLoadValvePwm() {
  if (!eepromIsValid()) return;  // keep compile-time defaults
  eepromRead(ADDR_VALVEPWM, (uint8_t*)valvePwm, sizeof(valvePwm));
  for (int i = 0; i < 6; i++) {
    int def = (i < 3) ? 100 : 50;
    if (valvePwm[i] < 0 || valvePwm[i] > 100) valvePwm[i] = def;
  }
}

void eepromSaveValvePwm() {
  eepromWrite(ADDR_MAGIC,    MAGIC_BYTES, 2);
  eepromWrite(ADDR_VALVEPWM, (uint8_t*)valvePwm, sizeof(valvePwm));
}

void eepromLoadPhaseMins() {
  if (!eepromIsValid()) return;  // keep defaults
  int vals[2];
  eepromRead(ADDR_PHASE_MINS, (uint8_t*)vals, sizeof(vals));
  if (vals[0] >= 1 && vals[0] <= 60) insidePhaseMins  = vals[0];
  if (vals[1] >= 1 && vals[1] <= 60) outsidePhaseMins = vals[1];
}

void eepromSavePhaseMins() {
  int vals[2] = {insidePhaseMins, outsidePhaseMins};
  eepromWrite(ADDR_MAGIC,      MAGIC_BYTES, 2);
  eepromWrite(ADDR_PHASE_MINS, (uint8_t*)vals, sizeof(vals));
}
