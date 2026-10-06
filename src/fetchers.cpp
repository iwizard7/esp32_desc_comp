#include "fetchers.h"
#include "settings.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include <Preferences.h>

WeatherData weather{};
AirData air{};
RatesData rates{};
String lastError;
String geoError;
String weatherError;
String airError;
String ratesError;
static SemaphoreHandle_t fetchMutex = nullptr;
static uint32_t nextFetchAllowedMs = 0;
static uint8_t fetchFailures = 0;

static void cacheWeather() {
  Preferences p;
  p.begin("weather", false);
  p.putBool("ok", weather.ok);
  p.putFloat("temp", weather.temp);
  p.putFloat("hum", weather.humidity);
  p.putFloat("wind", weather.wind);
  p.putInt("code", weather.code);
  p.putFloat("tmax", weather.tmax);
  p.putFloat("tmin", weather.tmin);
  p.putInt("prob", weather.precipProb);
  p.putFloat("rain", weather.rainMm);
  p.putString("rise", weather.sunrise);
  p.putString("set", weather.sunset);
  p.end();
}

static void cacheAir() {
  Preferences p;
  p.begin("weather", false);
  p.putBool("air_ok", air.ok);
  p.putFloat("aqi", air.aqi);
  p.putFloat("pm25", air.pm25);
  p.end();
}

static void cacheRates() {
  Preferences p;
  p.begin("weather", false);
  p.putBool("rates_ok", rates.ok);
  p.putFloat("usd", rates.usd);
  p.putFloat("eur", rates.eur);
  p.putFloat("cny", rates.cny);
  p.putString("rdate", rates.date);
  p.end();
}

void dataCacheLoad() {
  Preferences p;
  p.begin("weather", true);
  weather.ok = p.getBool("ok", false);
  weather.temp = p.getFloat("temp", 0);
  weather.humidity = p.getFloat("hum", 0);
  weather.wind = p.getFloat("wind", 0);
  weather.code = p.getInt("code", 0);
  weather.tmax = p.getFloat("tmax", 0);
  weather.tmin = p.getFloat("tmin", 0);
  weather.precipProb = p.getInt("prob", 0);
  weather.rainMm = p.getFloat("rain", 0);
  weather.sunrise = p.getString("rise", "");
  weather.sunset = p.getString("set", "");
  air.ok = p.getBool("air_ok", false);
  air.aqi = p.getFloat("aqi", 0);
  air.pm25 = p.getFloat("pm25", 0);
  rates.ok = p.getBool("rates_ok", false);
  rates.usd = p.getFloat("usd", 0);
  rates.eur = p.getFloat("eur", 0);
  rates.cny = p.getFloat("cny", 0);
  rates.date = p.getString("rdate", "");
  p.end();
  weather.fetchedAt = 0;
  air.fetchedAt = 0;
  rates.fetchedAt = 0;
  weather.rainInMin = -1;
}

static void initWeatherDefaults() {
  weather.rainInMin = -1;
}

static int hmToMin(const String& hm, int fallback) {
  if (hm.length() < 4) return fallback;
  int h = hm.substring(0, 2).toInt();
  int m = hm.substring(3).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

bool isNightNow() {
  if (!settings.nightMode) return false;
  time_t now = time(nullptr);
  if (now < 100000) return false;
  struct tm t;
  localtime_r(&now, &t);
  int nowm = t.tm_hour * 60 + t.tm_min;
  int rise = hmToMin(weather.sunrise, 7 * 60);
  int set = hmToMin(weather.sunset, 19 * 60);
  if (set == rise) return nowm >= 22 * 60 || nowm < 7 * 60;
  if (set > rise) return nowm >= set || nowm < rise;
  return nowm >= set && nowm < rise;
}

static String httpsGet(const String& url, const String& headerValue = "") {
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "no wifi";
    Serial.println("[HTTP] Skip - not connected");
    return "";
  }

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(20000);
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("DeskOLED/1.0");

  bool isHttps = url.startsWith("https://");
  String body = "";

  Serial.printf("[HTTP] GET %s\n", url.c_str());

  if (isHttps) {
    WiFiClientSecure secClient;
    secClient.setInsecure();
    secClient.setTimeout(20);
    if (http.begin(secClient, url)) {
      if (headerValue.length()) {
        http.addHeader("X-Yandex-Weather-Key", headerValue);
      }
      int code = http.GET();
      Serial.printf("[HTTP] Response: %d\n", code);
      if (code == HTTP_CODE_OK) {
        body = http.getString();
        Serial.printf("[HTTP] Body size: %d bytes\n", body.length());
      } else {
        lastError = "HTTPS-" + String(code);
        if (code < 0) {
          lastError = "HTTPS-" + String(code) + " (" + http.errorToString(code) + ")";
        }
        Serial.println("[HTTP] Error: " + lastError);
      }
      http.end();
    } else {
      lastError = "https-begin";
      Serial.println("[HTTP] https begin failed");
    }
  } else {
    WiFiClient tcpClient;
    tcpClient.setTimeout(20);
    if (http.begin(tcpClient, url)) {
      int code = http.GET();
      Serial.printf("[HTTP] Response: %d\n", code);
      if (code == HTTP_CODE_OK) {
        body = http.getString();
        Serial.printf("[HTTP] Body size: %d bytes\n", body.length());
      } else {
        lastError = "HTTP-" + String(code);
        if (code < 0) {
          lastError = "HTTP-" + String(code) + " (" + http.errorToString(code) + ")";
        }
        Serial.println("[HTTP] Error: " + lastError);
      }
      http.end();
    } else {
      lastError = "http-begin";
      Serial.println("[HTTP] http begin failed");
    }
  }
  return body;
}

static void urlEncode(const String& in, String& out) {
  out.reserve(in.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else if (c == ' ') {
      out += "%20";
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
}

void applyTimezone() {
  if (settings.timezone.length()) {
    setenv("TZ", settings.timezone.c_str(), 1);
    tzset();
    return;
  }
  int h = settings.utcOffset / 3600;
  char buf[16];
  if (h >= 0) {
    snprintf(buf, sizeof(buf), "UTC-%d", h);
  } else {
    snprintf(buf, sizeof(buf), "UTC+%d", -h);
  }
  setenv("TZ", buf, 1);
  tzset();
}

bool geocodeCity(const String& city) {
  Serial.printf("[GEO] Geocoding: %s\n", city.c_str());
  String enc;
  urlEncode(city, enc);
  String url = "http://geocoding-api.open-meteo.com/v1/search?name=" + enc +
               "&count=1&language=ru&format=json";
  String body = httpsGet(url);
  if (body.isEmpty()) {
    geoError = "network";
    Serial.println("[GEO] Empty body, keeping current coords");
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    geoError = "json";
    Serial.println("[GEO] JSON parse error");
    return false;
  }
  JsonArray results = doc["results"].as<JsonArray>();
  if (results.isNull() || results.size() == 0) {
    geoError = "город не найден";
    Serial.println("[GEO] City not found");
    return false;
  }
  JsonObject r = results[0];
  settings.lat = r["latitude"] | settings.lat;
  settings.lon = r["longitude"] | settings.lon;
  settings.cityLabel = r["name"] | city;
  settings.timezone = r["timezone"] | settings.timezone;
  settings.utcOffset = r["utc_offset_seconds"] | settings.utcOffset;
  settings.city = city;
  applyTimezone();
  geoError = "";
  Serial.printf("[GEO] OK: %s lat=%.4f lon=%.4f\n",
                settings.cityLabel.c_str(), settings.lat, settings.lon);
  return true;
}

static int wttrCode(int code) {
  if (code == 113) return 0;
  if (code == 116) return 1;
  if (code == 119) return 2;
  if (code == 122) return 3;
  if (code == 143 || code == 248 || code == 260) return 45;
  if (code == 200 || code >= 386) return 95;
  if (code == 227 || code == 230 || (code >= 323 && code <= 395)) return 71;
  if (code == 182 || code == 185 || code == 281 || code == 284 ||
      code == 317 || code == 320 || code == 350 || code == 362 ||
      code == 365 || code == 374 || code == 377) return 67;
  if (code == 176 || code == 263 || code == 266 || code == 293 ||
      code == 296 || (code >= 299 && code <= 359)) return 61;
  return 3;
}

static String localTime24(const char* value) {
  String s(value ? value : "");
  s.trim();
  if (s.length() < 7) return s;
  int hour = s.substring(0, 2).toInt();
  int minute = s.substring(3, 5).toInt();
  bool pm = s.endsWith("PM");
  if (s.endsWith("AM") && hour == 12) hour = 0;
  if (pm && hour < 12) hour += 12;
  char result[6];
  snprintf(result, sizeof(result), "%02d:%02d", hour, minute);
  return String(result);
}

bool fetchWeather() {
  Serial.printf("[WX] Fetch weather for lat=%.4f lon=%.4f\n", settings.lat, settings.lon);
  if (settings.lat == 0 && settings.lon == 0) {
    weatherError = "lat/lon=0";
    Serial.println("[WX] ERROR: coordinates are 0,0 - geocode not done");
    return false;
  }
  char url[180];
  snprintf(url, sizeof(url),
           "https://wttr.in/%.4f,%.4f?format=j1",
           settings.lat, settings.lon);
  String body = httpsGet(url);
  if (body.isEmpty()) {
    weatherError = "network";
    Serial.println("[WX] wttr.in request failed");
    return false;
  }

  JsonDocument filter;
  filter["current_condition"][0]["temp_C"] = true;
  filter["current_condition"][0]["humidity"] = true;
  filter["current_condition"][0]["windspeedKmph"] = true;
  filter["current_condition"][0]["weatherCode"] = true;
  filter["current_condition"][0]["precipMM"] = true;
  filter["weather"][0]["mintempC"] = true;
  filter["weather"][0]["maxtempC"] = true;
  filter["weather"][0]["astronomy"][0]["sunrise"] = true;
  filter["weather"][0]["astronomy"][0]["sunset"] = true;
  filter["weather"][0]["hourly"][0]["chanceofrain"] = true;
  filter["weather"][0]["hourly"][0]["precipMM"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    weatherError = "json";
    Serial.printf("[WX] JSON error: %s\n", err.c_str());
    return false;
  }

  JsonObject current = doc["current_condition"][0];
  JsonArray days = doc["weather"].as<JsonArray>();
  if (current.isNull() || days.isNull() || days.size() == 0) {
    weatherError = "fields";
    Serial.println("[WX] Missing current_condition/weather in wttr response");
    return false;
  }

  weather.temp = atof(current["temp_C"] | "0");
  weather.humidity = atof(current["humidity"] | "0");
  weather.wind = atof(current["windspeedKmph"] | "0") / 3.6f;
  weather.code = wttrCode(atoi(current["weatherCode"] | "0"));
  JsonObject today = days[0];
  weather.tmin = atof(today["mintempC"] | "0");
  weather.tmax = atof(today["maxtempC"] | "0");
  weather.precipProb = atoi(today["hourly"][0]["chanceofrain"] | "0");
  weather.rainMm = atof(current["precipMM"] | "0");
  JsonArray astronomy = today["astronomy"].as<JsonArray>();
  if (!astronomy.isNull() && astronomy.size() > 0) {
    weather.sunrise = localTime24(astronomy[0]["sunrise"] | "");
    weather.sunset = localTime24(astronomy[0]["sunset"] | "");
  }
  weather.rainInMin = -1;
  weather.ok = true;
  weather.fetchedAt = millis();
  cacheWeather();
  weatherError = "";
  Serial.printf("[WX] wttr OK: temp=%.1f humidity=%.0f wind=%.1f rain=%.1f\n",
                weather.temp, weather.humidity, weather.wind, weather.rainMm);
  return true;
}

bool fetchAir() {
  Serial.println("[AQI] Fetch air quality");
  if (settings.lat == 0 && settings.lon == 0) {
    airError = "lat/lon=0";
    return false;
  }
  char url[300];
  // Try HTTP first (works well), fallback to HTTPS
  snprintf(url, sizeof(url),
           "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f"
           "&longitude=%.4f&current=european_aqi,pm2_5",
           settings.lat, settings.lon);
  String body = httpsGet(url);
  if (body.isEmpty()) {
    snprintf(url, sizeof(url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f"
             "&longitude=%.4f&current=european_aqi,pm2_5",
             settings.lat, settings.lon);
    body = httpsGet(url);
  }
  if (body.isEmpty()) {
    airError = "network";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    airError = "json";
    return false;
  }
  air.aqi = doc["current"]["european_aqi"] | 0.0f;
  air.pm25 = doc["current"]["pm2_5"] | 0.0f;
  air.ok = true;
  air.fetchedAt = millis();
  cacheAir();
  airError = "";
  Serial.printf("[AQI] OK: AQI=%.0f PM2.5=%.1f\n", air.aqi, air.pm25);
  return true;
}

bool fetchRates() {
  Serial.println("[FX] Fetch rates from CBR");
  String body = httpsGet("https://www.cbr-xml-daily.ru/daily_json.js");
  if (body.isEmpty()) {
    ratesError = "network";
    return false;
  }
  JsonDocument filter;
  filter["Date"] = true;
  filter["Valute"]["USD"]["Value"] = true;
  filter["Valute"]["USD"]["Nominal"] = true;
  filter["Valute"]["EUR"]["Value"] = true;
  filter["Valute"]["EUR"]["Nominal"] = true;
  filter["Valute"]["CNY"]["Value"] = true;
  filter["Valute"]["CNY"]["Nominal"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    ratesError = "json";
    return false;
  }
  auto unit = [](JsonObject v) -> float {
    float value = v["Value"] | 0.0f;
    int nom = v["Nominal"] | 1;
    if (nom <= 0) nom = 1;
    return value / nom;
  };
  JsonObject valute = doc["Valute"].as<JsonObject>();
  if (valute.isNull()) {
    ratesError = "fields";
    return false;
  }
  rates.usd = unit(valute["USD"].as<JsonObject>());
  rates.eur = unit(valute["EUR"].as<JsonObject>());
  rates.cny = unit(valute["CNY"].as<JsonObject>());
  rates.date = doc["Date"] | "";
  if (rates.date.length() >= 10) rates.date = rates.date.substring(0, 10);
  rates.ok = true;
  rates.fetchedAt = millis();
  cacheRates();
  ratesError = "";
  return true;
}

bool fetchAllData(bool force) {
  uint32_t now = millis();
  if (!force && (int32_t)(now - nextFetchAllowedMs) < 0) return false;
  if (!fetchMutex) fetchMutex = xSemaphoreCreateMutex();
  if (!fetchMutex || xSemaphoreTake(fetchMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    lastError = "fetch-busy";
    return false;
  }

  bool ok = true;
  if (settings.city.length() && !geocodeCity(settings.city)) ok = false;
  settingsSave();
  if (!fetchWeather()) ok = false;
  if (!fetchAir()) ok = false;
  if (!fetchRates()) ok = false;

  if (ok) {
    fetchFailures = 0;
    nextFetchAllowedMs = millis() + 15000UL;
    lastError = "";
  } else {
    if (fetchFailures < 6) fetchFailures++;
    uint32_t delayMs = 30000UL << (fetchFailures > 4 ? 4 : fetchFailures - 1);
    if (delayMs > 15 * 60 * 1000UL) delayMs = 15 * 60 * 1000UL;
    nextFetchAllowedMs = millis() + delayMs;
    lastError = weatherError.length() ? weatherError : (airError.length() ? airError : ratesError);
  }
  xSemaphoreGive(fetchMutex);
  return ok;
}

const char* wmoLabel(int code) {
  if (code == 0) return "ясно";
  if (code == 1) return "почти ясно";
  if (code == 2) return "переменно";
  if (code == 3) return "пасмурно";
  if (code == 45 || code == 48) return "туман";
  if (code >= 51 && code <= 57) return "морось";
  if (code >= 61 && code <= 67) return "дождь";
  if (code >= 71 && code <= 77) return "снег";
  if (code >= 80 && code <= 82) return "ливень";
  if (code == 85 || code == 86) return "снегопад";
  if (code >= 95 && code <= 99) return "гроза";
  return "погода";
}

const char* wmoShortLabel(int code) {
  if (code == 0) return "ясно";
  if (code == 1) return "малообл.";
  if (code == 2) return "облачно";
  if (code == 3) return "пасмурно";
  if (code == 45 || code == 48) return "туман";
  if (code >= 51 && code <= 57) return "морось";
  if (code >= 61 && code <= 67) return "дождь";
  if (code >= 71 && code <= 77) return "снег";
  if (code >= 80 && code <= 82) return "ливень";
  if (code == 85 || code == 86) return "снегопад";
  if (code >= 95 && code <= 99) return "гроза";
  return "погода";
}

int moonPhase(int year, int month, int day) {
  // Simple Conway-like approximation -> 0 new ... 4 full
  if (month < 3) {
    year--;
    month += 12;
  }
  int a = year / 100;
  int b = a / 4;
  int c = 2 - a + b;
  int e = (int)(365.25 * (year + 4716));
  int f = (int)(30.6001 * (month + 1));
  double jd = c + day + e + f - 1524.5;
  double days = jd - 2451550.1;
  double lun = days / 29.53058867;
  lun = lun - floor(lun);
  if (lun < 0) lun += 1;
  return (int)(lun * 8 + 0.5) % 8;
}

const char* moonLabel(int phase) {
  static const char* names[] = {
      "новолуние", "раст. серп", "1 четверть", "раст. горб",
      "полнолуние", "убыв. горб", "3 четверть", "убыв. серп"};
  return names[phase & 7];
}
