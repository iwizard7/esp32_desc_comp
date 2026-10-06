#pragma once

#include <Arduino.h>

struct IndoorData {
  bool ok;
  uint8_t addr;
  float temp;
  float humidity;
  float pressure;  // hPa
  uint32_t fetchedAt;
};

extern IndoorData indoor;

void bmeBegin();
void bmePoll();
bool bmePresent();
