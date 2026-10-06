#pragma once

#include <Arduino.h>

#define FW_VERSION "0.4.0"

#define SLIDE_CLOCK 0
#define SLIDE_WEATHER 1
#define SLIDE_FORECAST 2
#define SLIDE_UMBRELLA 3
#define SLIDE_RATES 4
#define SLIDE_AIR 5
#define SLIDE_SUN 6
#define SLIDE_INDOOR 7
#define SLIDE_STATUS 8
#define SLIDE_COUNT 9

struct Settings {
  String ssid;
  String pass;
  String city;
  String cityLabel;
  float lat;
  float lon;
  int utcOffset;
  uint16_t intervalSec;  // default duration for new/empty rows
  uint8_t contrast;
  uint8_t nightContrast;
  bool flip;
  bool configured;
  bool nightMode;
  bool nightClockOnly;
  bool pixelShift;
  bool bmeEnabled;
  bool slideOn[SLIDE_COUNT];
  uint8_t slideOrder[SLIDE_COUNT];
  uint8_t slideSec[SLIDE_COUNT];
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsClearWifi();
void settingsFactoryReset();
bool settingsHasWifi();
uint8_t clampSlideSec(int v);
void settingsDefaultsSlides();
