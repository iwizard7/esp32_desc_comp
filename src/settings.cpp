#include "settings.h"
#include <Preferences.h>
#include <string.h>

Settings settings;
static Preferences prefs;

uint8_t clampSlideSec(int v) {
  if (v < 3) return 3;
  if (v > 120) return 120;
  return (uint8_t)v;
}

void settingsDefaultsSlides() {
  for (int i = 0; i < SLIDE_COUNT; i++) {
    settings.slideOn[i] = true;
    settings.slideOrder[i] = (uint8_t)(i + 1);
    settings.slideSec[i] = 6;
  }
  settings.slideOn[SLIDE_UMBRELLA] = false;
  settings.slideOn[SLIDE_INDOOR] = false;
  settings.slideOn[SLIDE_STATUS] = false;
}

void settingsLoad() {
  settingsDefaultsSlides();
  prefs.begin("desk", true);
  settings.ssid = prefs.getString("ssid", "");
  settings.pass = prefs.getString("pass", "");
  settings.city = prefs.getString("city", "Moscow");
  settings.cityLabel = prefs.getString("clabel", "Москва");
  settings.lat = prefs.getFloat("lat", 55.7558f);
  settings.lon = prefs.getFloat("lon", 37.6173f);
  settings.utcOffset = prefs.getInt("utc", 10800);
  settings.intervalSec = prefs.getUShort("ival", 6);
  if (settings.intervalSec < 3) settings.intervalSec = 3;
  if (settings.intervalSec > 120) settings.intervalSec = 120;
  settings.contrast = prefs.getUChar("contrast", 160);
  settings.nightContrast = prefs.getUChar("ncontrast", 28);
  settings.flip = prefs.getBool("flip", false);
  settings.configured = prefs.getBool("ok", false);
  settings.nightMode = prefs.getBool("night", true);
  settings.nightClockOnly = prefs.getBool("nclock", true);
  settings.pixelShift = prefs.getBool("pshift", true);
  settings.bmeEnabled = prefs.getBool("bme", false);

  uint8_t blob[SLIDE_COUNT * 3];
  size_t n = prefs.getBytes("scfg", blob, sizeof(blob));
  if (n == sizeof(blob)) {
    for (int i = 0; i < SLIDE_COUNT; i++) {
      settings.slideOn[i] = blob[i * 3] != 0;
      settings.slideOrder[i] = blob[i * 3 + 1];
      settings.slideSec[i] = clampSlideSec(blob[i * 3 + 2]);
    }
  } else {
    uint8_t oldMask = prefs.getUChar("slides", 0);
    if (oldMask) {
      const uint8_t mapBit[7] = {0, 1, 2, 4, 5, 6, 8};
      for (int i = 0; i < SLIDE_COUNT; i++) settings.slideOn[i] = false;
      for (int b = 0; b < 7; b++) {
        if (oldMask & (1 << b)) settings.slideOn[mapBit[b]] = true;
      }
      settings.slideOn[SLIDE_UMBRELLA] = true;
      for (int i = 0; i < SLIDE_COUNT; i++) {
        settings.slideSec[i] = (uint8_t)settings.intervalSec;
      }
    }
  }
  bool any = false;
  for (int i = 0; i < SLIDE_COUNT; i++) {
    if (settings.slideOn[i]) any = true;
  }
  if (!any) settings.slideOn[SLIDE_CLOCK] = true;
  prefs.end();
}

void settingsSave() {
  uint8_t blob[SLIDE_COUNT * 3];
  for (int i = 0; i < SLIDE_COUNT; i++) {
    blob[i * 3] = settings.slideOn[i] ? 1 : 0;
    blob[i * 3 + 1] = settings.slideOrder[i];
    blob[i * 3 + 2] = settings.slideSec[i];
  }
  prefs.begin("desk", false);
  prefs.putString("ssid", settings.ssid);
  prefs.putString("pass", settings.pass);
  prefs.putString("city", settings.city);
  prefs.putString("clabel", settings.cityLabel);
  prefs.putFloat("lat", settings.lat);
  prefs.putFloat("lon", settings.lon);
  prefs.putInt("utc", settings.utcOffset);
  prefs.putUShort("ival", settings.intervalSec);
  prefs.putUChar("contrast", settings.contrast);
  prefs.putUChar("ncontrast", settings.nightContrast);
  prefs.putBool("flip", settings.flip);
  prefs.putBool("ok", settings.configured);
  prefs.putBool("night", settings.nightMode);
  prefs.putBool("nclock", settings.nightClockOnly);
  prefs.putBool("pshift", settings.pixelShift);
  prefs.putBool("bme", settings.bmeEnabled);
  prefs.putBytes("scfg", blob, sizeof(blob));
  prefs.end();
}

void settingsClearWifi() {
  settings.ssid = "";
  settings.pass = "";
  settings.configured = false;
  settingsSave();
}

void settingsFactoryReset() {
  prefs.begin("desk", false);
  prefs.clear();
  prefs.end();
  settingsLoad();
  settings.configured = false;
  settings.ssid = "";
  settings.pass = "";
}

bool settingsHasWifi() {
  return settings.ssid.length() > 0;
}
