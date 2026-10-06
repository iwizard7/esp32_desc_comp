#pragma once

#include <Arduino.h>

struct WeatherData {
  bool ok;
  float temp;
  float humidity;
  float wind;
  int code;
  float tmax;
  float tmin;
  int precipProb;
  String sunrise;
  String sunset;
  int rainInMin;  // -1 none in next hour, 0 already raining, else minutes
  float rainMm;
  int rainProb;
  uint32_t fetchedAt;
};

struct AirData {
  bool ok;
  float aqi;
  float pm25;
  uint32_t fetchedAt;
};

struct RatesData {
  bool ok;
  float usd;
  float eur;
  float cny;
  String date;
  uint32_t fetchedAt;
};

extern WeatherData weather;
extern AirData air;
extern RatesData rates;
extern String lastError;

bool geocodeCity(const String& city);
bool fetchWeather();
bool fetchAir();
bool fetchRates();
void applyTimezone();
bool isNightNow();
const char* wmoLabel(int code);
int moonPhase(int year, int month, int day);
const char* moonLabel(int phase);
