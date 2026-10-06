#include "bme_sensor.h"
#include "display_ui.h"
#include "fetchers.h"
#include "pins.h"
#include "portal.h"
#include "settings.h"

#include <WiFi.h>
#include <time.h>

static uint32_t lastSlideMs = 0;
static bool connecting = false;
static uint32_t connectStart = 0;
static bool pausedRotation = false;

// FreeRTOS background task handle & signals
static TaskHandle_t fetchTaskHandle = nullptr;
static volatile bool bgFetchPending = false;
static volatile bool bgFetchForce = false;

static void ntpSync() {
  applyTimezone();
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

static void backgroundFetchTask(void* param) {
  while (true) {
    if (bgFetchPending && WiFi.status() == WL_CONNECTED) {
      bgFetchPending = false;
      ntpSync();
      fetchAllData(bgFetchForce);
      bgFetchForce = false;
      if (settings.bmeEnabled) {
        bmePoll();
      }
      displayInvalidate();
    }
    // Check every 500ms
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void triggerAsyncFetch(bool force = false) {
  bgFetchPending = true;
  if (force) bgFetchForce = true;
}

static void startSta() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("desk-c3");
  WiFi.setSleep(false);
  // Use Google DNS to bypass ISP DNS filtering
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE,
              IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4));
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  connecting = true;
  connectStart = millis();
  displayMessage("Wi-Fi...", settings.ssid.c_str());
}

static void enterAp(const char* why) {
  connecting = false;
  displayMessage("Точка доступа", why);
  delay(400);
  portalBeginAp();
  displayApInfo(portalApSsid().c_str(), portalApPass().c_str());
}

static void afterOnline() {
  portalBeginSta();
  connecting = false;
       triggerAsyncFetch(true);
  displayRebuildPlaylist();
  lastSlideMs = millis();
}

static void refreshDataTimer() {
  if (WiFi.status() != WL_CONNECTED) return;
  static uint32_t lastWxPoll = 0;
  static uint32_t lastFxPoll = 0;
  static uint32_t lastNtpPoll = 0;
  uint32_t now = millis();

  // Weather & air every 15 mins
  if (now - lastWxPoll > 15 * 60 * 1000UL || lastWxPoll == 0) {
    lastWxPoll = now;
    triggerAsyncFetch();
  }
  // Rates every 60 mins
  if (now - lastFxPoll > 60 * 60 * 1000UL || lastFxPoll == 0) {
    lastFxPoll = now;
    triggerAsyncFetch();
  }
  // NTP every 6 hours
  if (now - lastNtpPoll > 6 * 60 * 60 * 1000UL || lastNtpPoll == 0) {
    lastNtpPoll = now;
    ntpSync();
  }
}

// Button state machine for BOOT button (GPIO9):
// 1 click: next slide (with animation)
// 2 clicks: toggle pause rotation
// Long press (> 2.5s): open AP
static void handleBootButton() {
  static uint32_t pressStart = 0;
  static uint32_t releaseTime = 0;
  static uint8_t clickCount = 0;
  static bool wasPressed = false;
  static bool longHandled = false;

  bool isPressed = (digitalRead(PIN_BOOT) == LOW);
  uint32_t now = millis();

  if (isPressed && !wasPressed) {
    // Just pressed
    pressStart = now;
    wasPressed = true;
    longHandled = false;
  } else if (isPressed && wasPressed) {
    // Being held
    if (!longHandled && (now - pressStart > 2500)) {
      longHandled = true;
      clickCount = 0;
      if (!portalApActive()) {
        enterAp("кнопка BOOT");
      }
    }
  } else if (!isPressed && wasPressed) {
    // Just released
    wasPressed = false;
    if (!longHandled) {
      clickCount++;
      releaseTime = now;
    }
  }

  // Multi-click timeout check (300ms window)
  if (clickCount > 0 && !isPressed && (now - releaseTime > 300)) {
    if (clickCount == 1) {
      // Single click: advance slide manually
      displayAdvance(true);
      lastSlideMs = now;
    } else if (clickCount >= 2) {
      // Double click: pause / resume auto-rotation
      pausedRotation = !pausedRotation;
      displayMessage(pausedRotation ? "Пауза ротации" : "Авторотация", "слайдов");
      delay(600);
      lastSlideMs = now;
    }
    clickCount = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(150);
  pinMode(PIN_BOOT, INPUT_PULLUP);

  settingsLoad();
  dataCacheLoad();
  displayBegin();
  displayRebuildPlaylist();
  if (settings.bmeEnabled) {
    bmeBegin();
  }

  // Create background FreeRTOS async fetching task with 12KB stack for mbedTLS / HTTPS
  xTaskCreate(
      backgroundFetchTask,
      "bg_fetch",
      12288,
      nullptr,
      1,
      &fetchTaskHandle
  );

  displayMessage("Desk OLED", "ESP32-C3", "старт...");

  if (!settingsHasWifi()) {
    enterAp("первый запуск");
    return;
  }
  startSta();
}

void loop() {
  portalLoop();
  handleBootButton();

  if (connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      afterOnline();
    } else if (millis() - connectStart > 25000) {
      enterAp("Wi-Fi не найден");
    }
    delay(20);
    return;
  }

  if (portalApActive()) {
    static uint32_t lastApDraw = 0;
    if (millis() - lastApDraw > 1000) {
      displayApInfo(portalApSsid().c_str(), portalApPass().c_str());
      lastApDraw = millis();
    }
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    displayMessage("Сеть пропала", "переподключение");
    startSta();
    return;
  }

  refreshDataTimer();

  // Read commands via USB Serial:
  // 's' -> print JSON status
  // 'f' -> trigger immediate fetch
  // 'r' -> reboot
  if (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == 's') {
       Serial.printf("{\"wifi\":\"%s\",\"ip\":\"%s\",\"city\":\"%s\",\"tz\":\"%s\",\"lat\":%.4f,\"lon\":%.4f,\"wx\":%d,\"air\":%d,\"rates\":%d,\"weatherError\":\"%s\",\"airError\":\"%s\",\"ratesError\":\"%s\",\"err\":\"%s\"}\n",
                     WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                     WiFi.localIP().toString().c_str(),
                     settings.city.c_str(),
                     settings.timezone.c_str(),
                     settings.lat,
                     settings.lon,
                     weather.ok ? 1 : 0,
                     air.ok ? 1 : 0,
                     rates.ok ? 1 : 0,
                     weatherError.c_str(),
                     airError.c_str(),
                     ratesError.c_str(),
                     lastError.c_str());
    } else if (ch == 'f') {
      Serial.println("[CMD] Triggering fetch...");
      triggerAsyncFetch(true);
    } else if (ch == 'r') {
      Serial.println("[CMD] Rebooting...");
      delay(200);
      ESP.restart();
    }
  }

  uint32_t now = millis();
  uint32_t duration = displayCurrentDurationMs();
  if (!pausedRotation && (now - lastSlideMs >= duration)) {
    displayAdvance(true);
    lastSlideMs = now;
  } else {
    displayShowCurrent();
  }

  delay(50);
}
