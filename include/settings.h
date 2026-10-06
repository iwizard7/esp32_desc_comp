#pragma once

#include <Arduino.h>

#define FW_VERSION "0.8.0"

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
#define LAYOUT_MAX_ELEMENTS 8

enum LayoutElementType : uint8_t {
  LAYOUT_TIME = 0,
  LAYOUT_DATE,
  LAYOUT_CITY,
  LAYOUT_TEMPERATURE,
  LAYOUT_CONDITION,
  LAYOUT_HUMIDITY_WIND,
  LAYOUT_FORECAST,
  LAYOUT_PRECIP,
  LAYOUT_RATES,
  LAYOUT_AQI,
  LAYOUT_PM25,
  LAYOUT_SUNRISE,
  LAYOUT_SUNSET,
  LAYOUT_INDOOR,
  LAYOUT_STATUS,
};

struct LayoutElement {
  uint8_t type;
  uint8_t font;
  uint8_t align;
  uint8_t enabled;
  int8_t x;
  int8_t y;
};

struct ScreenLayout {
  uint8_t custom;
  LayoutElement elements[LAYOUT_MAX_ELEMENTS];
};

struct Settings {
  String ssid;
  String pass;
  String city;
  String cityLabel;
  String timezone;
  String webPass;
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
  ScreenLayout layouts[SLIDE_COUNT];
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsClearWifi();
void settingsFactoryReset();
bool settingsHasWifi();
uint8_t clampSlideSec(int v);
void settingsDefaultsSlides();
void settingsDefaultsLayouts();
void settingsResetLayout(uint8_t screen);
