#include "display_ui.h"
#include "fetchers.h"
#include "pins.h"
#include "portal.h"
#include "settings.h"
#include "bme_sensor.h"

#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static int8_t shiftX = 0;
static int8_t shiftY = 0;

static void sendDisplayBuffer() {
  if (shiftX) {
    uint8_t* buf = u8g2.getBufferPtr();
    for (int page = 0; page < 4; page++) {
      uint8_t* row = buf + page * 128;
      for (int x = 127; x > 0; x--) row[x] = (uint8_t)((row[x] >> 1) | (row[x - 1] << 7));
      row[0] >>= 1;
    }
  }
  u8g2.sendBuffer();
}

static const char* wdRu(int wday) {
  static const char* d[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
  if (wday < 0 || wday > 6) return "--";
  return d[wday];
}

static const char* monRu(int mon) {
  static const char* m[] = {"янв", "фев", "мар", "апр", "май", "июн",
                            "июл", "авг", "сен", "окт", "ноя", "дек"};
  if (mon < 0 || mon > 11) return "---";
  return m[mon];
}

void displayBegin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  u8g2.setI2CAddress(OLED_ADDR << 1);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  displaySetContrast(settings.contrast);
  displaySetFlip(settings.flip);
}

void displaySetFlip(bool flip) {
  u8g2.setFlipMode(flip ? 1 : 0);
}

void displaySetContrast(uint8_t contrast) {
  u8g2.setContrast(contrast);
}

void displayMessage(const char* line1, const char* line2, const char* line3) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (line1 && line1[0]) u8g2.drawUTF8(0, 10, line1);
  if (line2 && line2[0]) u8g2.drawUTF8(0, 21, line2);
  if (line3 && line3[0]) u8g2.drawUTF8(0, 32, line3);
       u8g2.sendBuffer();
}

void displayApInfo(const char* ssid, const char* pass) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_t_cyrillic);
  u8g2.drawUTF8(0, 8, "Настройка Wi-Fi");
  u8g2.drawUTF8(0, 18, ssid);
  char buf[32];
  snprintf(buf, sizeof(buf), "пароль %s", pass);
  u8g2.drawUTF8(0, 26, buf);
  u8g2.drawUTF8(0, 32, "http://192.168.4.1");
  sendDisplayBuffer();
}

void displayClock() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  u8g2.clearBuffer();
  if (now < 100000) {
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(0, 14, "Ждём NTP...");
    u8g2.drawUTF8(0, 28, settings.cityLabel.c_str());
  } else {
    char hm[8];
    snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    u8g2.setFont(u8g2_font_logisoso24_tn);
    u8g2.drawStr(0, 24, hm);
    char line[40];
    snprintf(line, sizeof(line), "%s %d %s", wdRu(t.tm_wday), t.tm_mday, monRu(t.tm_mon));
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.drawUTF8(0, 32, line);
  }
  sendDisplayBuffer();
}

void displayWeather() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (!weather.ok) {
    u8g2.drawUTF8(0, 14, "Нет погоды");
     u8g2.drawUTF8(0, 28, weatherError.c_str());
  } else {
    char t[16];
    snprintf(t, sizeof(t), "%+.0f°", weather.temp);
    u8g2.setFont(u8g2_font_logisoso16_tr);
    u8g2.drawStr(0, 18, t);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
     u8g2.drawUTF8(56, 12, wmoShortLabel(weather.code));
    char sub[40];
    snprintf(sub, sizeof(sub), "%s  %d%%  %.0fм/с", settings.cityLabel.c_str(),
             (int)weather.humidity, weather.wind);
    u8g2.drawUTF8(0, 32, sub);
  }
  sendDisplayBuffer();
}

void displayForecast() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (!weather.ok) {
    u8g2.drawUTF8(0, 20, "Нет прогноза");
  } else {
    u8g2.drawUTF8(0, 10, "Сегодня");
    char line[40];
    snprintf(line, sizeof(line), "мин %+.0f°  макс %+.0f°", weather.tmin, weather.tmax);
    u8g2.drawUTF8(0, 21, line);
    snprintf(line, sizeof(line), "осадки %d%% %.1fмм", weather.precipProb, weather.rainMm);
    u8g2.drawUTF8(0, 32, line);
  }
  sendDisplayBuffer();
}

void displayRates() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (!rates.ok) {
    u8g2.drawUTF8(0, 14, "Нет курсов ЦБ");
     u8g2.drawUTF8(0, 28, ratesError.c_str());
  } else {
    char a[24], b[24], c[24];
    snprintf(a, sizeof(a), "USD  %.2f", rates.usd);
    snprintf(b, sizeof(b), "EUR  %.2f", rates.eur);
    snprintf(c, sizeof(c), "CNY  %.2f", rates.cny);
    u8g2.drawUTF8(0, 10, a);
    u8g2.drawUTF8(0, 21, b);
    u8g2.drawUTF8(0, 32, c);
  }
  sendDisplayBuffer();
}

static const char* aqiHint(float aqi) {
  if (aqi <= 20) return "отлично";
  if (aqi <= 40) return "хорошо";
  if (aqi <= 60) return "норм.";
  if (aqi <= 80) return "плохо";
  if (aqi <= 100) return "вредно";
  return "опасно";
}

void displayAir() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (!air.ok) {
    u8g2.drawUTF8(0, 20, "Нет AQI");
  } else {
    char line[40];
    snprintf(line, sizeof(line), "AQI %.0f  %s", air.aqi, aqiHint(air.aqi));
    u8g2.drawUTF8(0, 12, line);
    snprintf(line, sizeof(line), "PM2.5  %.1f ug/m3", air.pm25);
    u8g2.drawUTF8(0, 26, line);
  }
  sendDisplayBuffer();
}

void displaySun() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char line[48];
  if (weather.ok) {
    snprintf(line, sizeof(line), "восход %s", weather.sunrise.c_str());
    u8g2.drawUTF8(0, 10, line);
    snprintf(line, sizeof(line), "закат  %s", weather.sunset.c_str());
    u8g2.drawUTF8(0, 21, line);
  } else {
    u8g2.drawUTF8(0, 14, "нет солнца");
  }
  u8g2.drawUTF8(0, 32, settings.cityLabel.c_str());
  sendDisplayBuffer();
}

void displayStatus() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_t_cyrillic);
  char line[48];
  snprintf(line, sizeof(line), "IP %s", WiFi.localIP().toString().c_str());
  u8g2.drawUTF8(0, 8, line);
  snprintf(line, sizeof(line), "RSSI %d dBm", WiFi.RSSI());
  u8g2.drawUTF8(0, 17, line);
  snprintf(line, sizeof(line), "слайд %u сек", settings.intervalSec);
  u8g2.drawUTF8(0, 26, line);
  u8g2.drawUTF8(0, 32, settings.cityLabel.c_str());
  sendDisplayBuffer();
}

void displayUmbrella() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (!weather.ok) {
    u8g2.drawUTF8(0, 18, "Осадки: нет данных");
  } else if (weather.rainInMin == 0) {
    u8g2.drawUTF8(0, 12, "Дождь уже идёт!");
    char line[40];
    snprintf(line, sizeof(line), "%.1f мм  вер. %d%%", weather.rainMm, weather.rainProb);
    u8g2.drawUTF8(0, 26, line);
  } else if (weather.rainInMin > 0) {
    char line[40];
    snprintf(line, sizeof(line), "Дождь через ~%d мин", weather.rainInMin);
    u8g2.drawUTF8(0, 12, line);
    snprintf(line, sizeof(line), "%.1f мм  вер. %d%%", weather.rainMm, weather.rainProb);
    u8g2.drawUTF8(0, 26, line);
  } else {
    u8g2.drawUTF8(0, 12, "Без дождя");
    char line[40];
    snprintf(line, sizeof(line), "в ближ. час (вер. %d%%)", weather.precipProb);
    u8g2.drawUTF8(0, 26, line);
  }
  sendDisplayBuffer();
}

void displayIndoor() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  if (!bmePresent()) {
    u8g2.drawUTF8(0, 12, "Датчик BME280");
    u8g2.drawUTF8(0, 26, settings.bmeEnabled ? "не обнаружен" : "отключен в настр.");
  } else {
    char t[24];
    snprintf(t, sizeof(t), "%+.1f°C", indoor.temp);
    u8g2.setFont(u8g2_font_logisoso16_tr);
    u8g2.drawStr(0, 20, t);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    char sub[40];
    snprintf(sub, sizeof(sub), "В комн. %.0f%% %.0fмм", indoor.humidity, indoor.pressure * 0.750062f);
    u8g2.drawUTF8(0, 32, sub);
  }
  sendDisplayBuffer();
}

static uint8_t playlist[SLIDE_COUNT];
static uint8_t playlistCount = 0;
static uint8_t playlistIndex = 0;

void displayRebuildPlaylist() {
  playlistCount = 0;
  for (int order = 1; order <= SLIDE_COUNT; order++) {
    for (int i = 0; i < SLIDE_COUNT; i++) {
       if (i != SLIDE_UMBRELLA && settings.slideOn[i] && settings.slideOrder[i] == order) {
        playlist[playlistCount++] = (uint8_t)i;
      }
    }
  }
  // If order wasn't set or count is 0, add whatever is enabled
  if (playlistCount == 0) {
    for (int i = 0; i < SLIDE_COUNT; i++) {
       if (i != SLIDE_UMBRELLA && settings.slideOn[i]) playlist[playlistCount++] = (uint8_t)i;
    }
  }
  if (playlistCount == 0) {
    playlist[0] = SLIDE_CLOCK;
    playlistCount = 1;
  }
  if (playlistIndex >= playlistCount) playlistIndex = 0;
}

uint8_t displayCurrentSlideId() {
  if (playlistCount == 0) displayRebuildPlaylist();
  return playlist[playlistIndex];
}

uint32_t displayCurrentDurationMs() {
  uint8_t id = displayCurrentSlideId();
  uint8_t sec = settings.slideSec[id];
  if (sec < 3) sec = (settings.intervalSec >= 3 ? settings.intervalSec : 6);
  return (uint32_t)sec * 1000UL;
}

void renderSlideById(uint8_t id) {
  switch (id) {
    case SLIDE_CLOCK: displayClock(); break;
    case SLIDE_WEATHER: displayWeather(); break;
    case SLIDE_FORECAST: displayForecast(); break;
    case SLIDE_UMBRELLA: displayUmbrella(); break;
    case SLIDE_RATES: displayRates(); break;
    case SLIDE_AIR: displayAir(); break;
    case SLIDE_SUN: displaySun(); break;
    case SLIDE_INDOOR: displayIndoor(); break;
    case SLIDE_STATUS: displayStatus(); break;
    default: displayClock(); break;
  }
}

// Pixel shifting for burn-in protection: shifts display offsets periodically
static uint32_t lastShiftMs = 0;
static bool displayDirty = true;

static void updatePixelShift() {
  if (!settings.pixelShift) {
    shiftX = 0;
    shiftY = 0;
    return;
  }
  uint32_t now = millis();
  if (now - lastShiftMs > 10 * 60 * 1000UL) { // every 10 mins
    lastShiftMs = now;
    // Walk through 4 offsets: (0,0), (1,0), (1,1), (0,1)
    static uint8_t seq = 0;
    seq = (seq + 1) % 4;
    shiftX = (seq & 1) ? 1 : 0;
    shiftY = (seq & 2) ? 1 : 0;
    displayDirty = true;
  }
}

void displayApplyTheme() {
  bool night = isNightNow();
  if (night) {
    displaySetContrast(settings.nightContrast);
  } else {
    displaySetContrast(settings.contrast);
  }
}

void displayAdvance(bool animate) {
  if (playlistCount == 0) displayRebuildPlaylist();

  if (settings.nightMode && settings.nightClockOnly && isNightNow()) {
    playlistIndex = 0;
    displayApplyTheme();
    displayClock();
    return;
  }

  uint8_t nextIdx = (playlistIndex + 1) % playlistCount;
  
  if (animate) {
    // Smooth scroll transition out (scroll up 32 pixels)
    // We snapshot current buffer and slide in the new one
    // Using hardware/buffer scroll effect:
    uint8_t* buf = u8g2.getBufferPtr();
    size_t bufSize = 128 * (32 / 8); // 512 bytes for 128x32
    uint8_t prevBuf[512];
    memcpy(prevBuf, buf, bufSize);

    // Render next slide into u8g2 buffer
    renderSlideById(playlist[nextIdx]);
    uint8_t nextBuf[512];
    memcpy(nextBuf, buf, bufSize);

    // Smooth vertical scroll animation over 160ms (4 steps of 8 pixels)
    for (int step = 1; step <= 4; step++) {
      int offset = step * 8; // 8, 16, 24, 32
      // Shift upper part from prevBuf and lower part from nextBuf
      // For 128x32 page height = 4 pages (each 8px)
      int shiftPages = step; // 1 to 4 pages
      for (int page = 0; page < 4; page++) {
        if (page + shiftPages < 4) {
          memcpy(buf + page * 128, prevBuf + (page + shiftPages) * 128, 128);
        } else {
          int srcPage = page + shiftPages - 4;
          memcpy(buf + page * 128, nextBuf + srcPage * 128, 128);
        }
      }
      sendDisplayBuffer();
      delay(35);
    }
  }

  playlistIndex = nextIdx;
  displayApplyTheme();
  renderSlideById(playlist[playlistIndex]);
}

void displayShowCurrent() {
  updatePixelShift();
  static time_t lastSecond = 0;
  static uint8_t lastSlide = 255;
  static bool lastNightOnly = false;
  time_t now = time(nullptr);
  bool nightOnly = settings.nightMode && settings.nightClockOnly && isNightNow();
  uint8_t slide = displayCurrentSlideId();
  if (!displayDirty && now == lastSecond && slide == lastSlide && nightOnly == lastNightOnly) return;
  displayApplyTheme();
  if (nightOnly) {
    displayClock();
  } else {
    renderSlideById(slide);
  }
  lastSecond = now;
  lastSlide = slide;
  lastNightOnly = nightOnly;
  displayDirty = false;
}

void displayInvalidate() { displayDirty = true; }
