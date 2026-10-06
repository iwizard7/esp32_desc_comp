#include "bme_sensor.h"
#include "settings.h"

#include <Adafruit_BME280.h>
#include <Wire.h>

IndoorData indoor{};
static Adafruit_BME280 bme;
static bool started = false;

void bmeBegin() {
  indoor = IndoorData{};
  started = false;
  if (!settings.bmeEnabled) return;
  if (bme.begin(0x76, &Wire)) {
    indoor.addr = 0x76;
    started = true;
    bmePoll();
    return;
  }
  if (bme.begin(0x77, &Wire)) {
    indoor.addr = 0x77;
    started = true;
    bmePoll();
    return;
  }
  indoor.ok = false;
}

bool bmePresent() { return started && indoor.ok; }

void bmePoll() {
  if (!settings.bmeEnabled || !started) {
    indoor.ok = false;
    return;
  }
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0f;
  if (isnan(t) || t < -40 || t > 85) {
    indoor.ok = false;
    return;
  }
  indoor.temp = t;
  indoor.humidity = h;
  indoor.pressure = p;
  indoor.ok = true;
  indoor.fetchedAt = millis();
}
