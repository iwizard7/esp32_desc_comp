#include "portal.h"
#include "display_ui.h"
#include "fetchers.h"
#include "settings.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

static DNSServer dns;
static WebServer server(80);
static bool apActive = false;
static String apSsid = "DeskC3";
static const char* AP_PASS = "desk1234";

String portalApSsid() { return apSsid; }
String portalApPass() { return String(AP_PASS); }
bool portalApActive() { return apActive; }

static String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static bool authorized() {
  if (server.authenticate("admin", settings.webPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Desk OLED");
  return false;
}

static String scanOptions() {
  String html;
  int n = WiFi.scanComplete();
  if (n < 0) {
    n = WiFi.scanNetworks();
  }
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    html += "<option value=\"";
    html += htmlEscape(ssid);
    html += "\">";
    html += htmlEscape(ssid);
    html += " (";
    html += String(WiFi.RSSI(i));
    html += " dBm)</option>";
  }
  return html;
}

static bool chk(uint8_t id) { return id < SLIDE_COUNT && settings.slideOn[id]; }

static String page() {
  String s;
  s.reserve(6000);
  s += F("<!DOCTYPE html><html lang=ru><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>DeskC3</title><style>"
         "body{font:16px/1.4 system-ui,sans-serif;margin:0;background:#111;color:#eee}"
         "main{max-width:440px;margin:0 auto;padding:16px}"
         "h1{font-size:1.2rem;margin:0 0 12px}"
         "label,p{display:block;margin:12px 0 4px}"
          "input,select,button{width:100%;box-sizing:border-box;padding:10px;min-height:44px;border-radius:8px;"
         "border:1px solid #444;background:#1c1c1c;color:#eee;font-size:16px}"
         "button{background:#3b82f6;border:0;margin-top:16px;font-weight:600;cursor:pointer}"
         "button.alt{background:#444}"
         "button.danger{background:#b91c1c}"
          ".row{display:flex;gap:8px;align-items:center;margin:6px 0}"
          ".row input{width:auto}"
          ".slide-row{display:grid;grid-template-columns:24px 1fr 52px 60px;gap:6px;align-items:center;margin:8px 0}"
          ".slide-row input{min-height:36px;padding:6px}"
          ".slide-row input[type=checkbox]{width:auto}"
         ".card{background:#1a1a1a;padding:12px;border-radius:12px;margin:12px 0}"
         "small{color:#aaa}"
         "</style></head><body><main><h1>ESP32 Desk OLED</h1>");
  if (WiFi.status() == WL_CONNECTED) {
    s += "<div class=card>Сеть: ";
    s += htmlEscape(WiFi.SSID());
    s += "<br>IP: ";
    s += WiFi.localIP().toString();
    s += "<br>Город: ";
    s += htmlEscape(settings.cityLabel);
    s += "</div>";
  } else {
    s += F("<div class=card>Режим настройки. Подключитесь к точке доступа "
           "и сохраните домашнюю Wi‑Fi сеть.</div>");
  }
  s += F("<form method=POST action=/save>");
  s += F("<p>Wi‑Fi сеть</p><select name=ssid_sel><option value=''>— выбрать —</option>");
  s += scanOptions();
  s += "</select><p>или ввести SSID вручную</p><input name=ssid placeholder='SSID' value='";
  s += htmlEscape(settings.ssid);
  s += F("'><p>Пароль Wi‑Fi</p><input name=pass type=password placeholder='пароль'>");
  s += F("<p>Пароль веб-интерфейса</p><input name=webpass type=password placeholder='admin пароль'><small>Логин: admin. Пустое поле сохраняет текущий пароль.</small>");
  s += F("<p>Город (погода, время, дата)</p><input name=city required value='");
  s += htmlEscape(settings.city);
  s += F("'><p>Интервал слайдов по умолчанию, сек (3–120)</p><input name=interval type=number min=3 max=120 value='");
  s += String(settings.intervalSec);
  s += F("'><p>Яркость OLED день (0–255)</p><input name=contrast type=number min=0 max=255 value='");
  s += String(settings.contrast);
  s += F("'><p>Яркость OLED ночь (0–255)</p><input name=ncontrast type=number min=0 max=255 value='");
  s += String(settings.nightContrast);
  s += F("'><div class=row><input type=checkbox name=flip id=flip");
  if (settings.flip) s += " checked";
  s += F("><label for=flip>перевернуть дисплей на 180°</label></div>");

  s += F("<div class=row><input type=checkbox name=night id=night");
  if (settings.nightMode) s += " checked";
  s += F("><label for=night>ночной режим (по закату/восходу)</label></div>");

  s += F("<div class=row><input type=checkbox name=nclock id=nclock");
  if (settings.nightClockOnly) s += " checked";
  s += F("><label for=nclock>ночью показывать только часы</label></div>");

  s += F("<div class=row><input type=checkbox name=pshift id=pshift");
  if (settings.pixelShift) s += " checked";
  s += F("><label for=pshift>защита OLED от выгорания (Pixel Shift)</label></div>");

  s += F("<div class=row><input type=checkbox name=bme id=bme");
  if (settings.bmeEnabled) s += " checked";
  s += F("><label for=bme>опрос датчика BME280</label></div>");

  s += F("<div class=card><p><b>Экраны и ротация</b></p>");
  auto box = [&](const char* name, uint8_t id, const char* label, const char* token) {
    s += "<div class=slide-row><input type=checkbox name=";
    s += name;
    s += " id=";
    s += name;
    if (chk(id)) s += " checked";
    s += "><label for=";
    s += name;
    s += ">";
    s += label;
    s += "</label><input class=mini type=number min=1 max=9 name=ord_";
    s += token;
    s += " value='";
    s += String(settings.slideOrder[id]);
    s += "' title='порядок'><input class=mini type=number min=3 max=120 name=sec_";
    s += token;
    s += " value='";
    s += String(settings.slideSec[id]);
    s += "' title='секунды'></div>";
  };
  box("s_clock", SLIDE_CLOCK, "время и дата", "clock");
  box("s_wx", SLIDE_WEATHER, "погода сейчас", "wx");
  box("s_fc", SLIDE_FORECAST, "мин/макс и осадки", "fc");
  box("s_fx", SLIDE_RATES, "курсы ЦБ (USD EUR CNY)", "fx");
  box("s_air", SLIDE_AIR, "качество воздуха (AQI/PM2.5)", "air");
  box("s_sun", SLIDE_SUN, "восход и закат", "sun");
  box("s_in", SLIDE_INDOOR, "комнатный климат (BME280)", "in");
  box("s_st", SLIDE_STATUS, "IP и уровень Wi‑Fi", "st");
   s += F("</div><small>Осадки теперь показываются вместе с прогнозом. Отдельный экран дождя отключён.</small>"
          "<button type=submit>Сохранить и подключить</button></form>"
         "<form method=POST action=/rescan><button class=alt type=submit>Обновить список сетей</button></form>"
         "<form method=POST action=/ap><button class=alt type=submit>Только точка доступа</button></form>"
         "<form method=POST action=/reset onsubmit=\"return confirm('Сбросить все настройки?')\">"
         "<button class=danger type=submit>Заводской сброс</button></form>"
         "<div class=card><p><b>Обновление прошивки по воздуху (OTA)</b></p>"
         "<form method=POST action=/update enctype=multipart/form-data>"
         "<input type=file name=firmware accept='.bin' required style='margin-bottom:8px'>"
         "<button type=submit class=alt>Прошить .bin</button></form></div>"
         "<p><small><b>Кнопка BOOT (GPIO9):</b><br>"
         "• 1 клик: следующий слайд<br>"
         "• 2 клика: пауза / продолжение авторотации<br>"
         "• Удержание 3 сек: запуск точки доступа AP</small></p>"
         "<div class=card><p><b>Диагностика</b></p>"
         "<a href=/diag style='display:block;padding:10px;background:#1f6feb;color:#fff;"
         "border-radius:8px;text-align:center;text-decoration:none;font-weight:600'>"
         "&#128269; Открыть панель диагностики</a></div>"
         "</main></body></html>");
  return s;
}

static void handleRoot() {
  if (!authorized()) return;
  server.send(200, "text/html; charset=utf-8", page());
}

static void handleNotFound() {
  if (apActive) {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

static uint8_t slideNumber(const char* prefix, const char* token, uint8_t fallback,
                           int minValue, int maxValue) {
  String name = String(prefix) + token;
  int value = server.hasArg(name) ? server.arg(name).toInt() : fallback;
  if (value < minValue) value = minValue;
  if (value > maxValue) value = maxValue;
  return (uint8_t)value;
}

static void parseSlidesFromArgs() {
  settings.slideOn[SLIDE_CLOCK] = server.hasArg("s_clock");
  settings.slideOn[SLIDE_WEATHER] = server.hasArg("s_wx");
  settings.slideOn[SLIDE_FORECAST] = server.hasArg("s_fc");
  settings.slideOn[SLIDE_UMBRELLA] = server.hasArg("s_umb");
  settings.slideOn[SLIDE_RATES] = server.hasArg("s_fx");
  settings.slideOn[SLIDE_AIR] = server.hasArg("s_air");
  settings.slideOn[SLIDE_SUN] = server.hasArg("s_sun");
  settings.slideOn[SLIDE_INDOOR] = server.hasArg("s_in");
  settings.slideOn[SLIDE_STATUS] = server.hasArg("s_st");

  const char* tokens[SLIDE_COUNT] = {"clock", "wx", "fc", "umb", "fx", "air", "sun", "in", "st"};
  for (int i = 0; i < SLIDE_COUNT; i++) {
    settings.slideOrder[i] = slideNumber("ord_", tokens[i], settings.slideOrder[i], 1, SLIDE_COUNT);
    settings.slideSec[i] = clampSlideSec(slideNumber("sec_", tokens[i], settings.slideSec[i], 3, 120));
  }
  // The old rain screen is now merged into the forecast screen.
  settings.slideOn[SLIDE_UMBRELLA] = false;

  bool any = false;
  for (int i = 0; i < SLIDE_COUNT; i++) {
    if (settings.slideOn[i]) any = true;
  }
  if (!any) settings.slideOn[SLIDE_CLOCK] = true;
}

static void handleSave() {
  if (!authorized()) return;
  String sel = server.arg("ssid_sel");
  String typed = server.arg("ssid");
  typed.trim();
  if (sel.length()) settings.ssid = sel;
  else if (typed.length()) settings.ssid = typed;

  String pass = server.arg("pass");
  if (pass.length()) settings.pass = pass;

  String webpass = server.arg("webpass");
  webpass.trim();
  if (webpass.length() >= 6) settings.webPass = webpass;

  String city = server.arg("city");
  city.trim();
  if (city.length()) settings.city = city;

  int ival = server.arg("interval").toInt();
  if (ival < 3) ival = 3;
  if (ival > 120) ival = 120;
  settings.intervalSec = (uint16_t)ival;

  int c = server.arg("contrast").toInt();
  if (c < 0) c = 0;
  if (c > 255) c = 255;
  settings.contrast = (uint8_t)c;

  int nc = server.arg("ncontrast").toInt();
  if (nc < 0) nc = 0;
  if (nc > 255) nc = 255;
  settings.nightContrast = (uint8_t)nc;

  settings.flip = server.hasArg("flip");
  settings.nightMode = server.hasArg("night");
  settings.nightClockOnly = server.hasArg("nclock");
  settings.pixelShift = server.hasArg("pshift");
  settings.bmeEnabled = server.hasArg("bme");

  parseSlidesFromArgs();
  settings.configured = settings.ssid.length() > 0;
  settingsSave();

  displaySetContrast(settings.contrast);
  displaySetFlip(settings.flip);
  displayRebuildPlaylist();
  displayMessage("Сохранено", "геокод города...", settings.city.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    fetchAllData(true);
  }

  server.send(200, "text/html; charset=utf-8",
              F("<!DOCTYPE html><meta charset=utf-8><meta http-equiv=refresh content='3;url=/'>"
                "<body style='background:#111;color:#eee;font-family:sans-serif;padding:24px'>"
                "Сохранено. Плата перезагрузится...</body>"));
  delay(400);
  ESP.restart();
}

static void handleReset() {
  if (!authorized()) return;
  settingsFactoryReset();
  server.send(200, "text/plain; charset=utf-8", "OK, reboot");
  delay(300);
  ESP.restart();
}

static void handleRescan() {
  if (!authorized()) return;
  WiFi.scanDelete();
  WiFi.scanNetworks();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

static void handleForceAp() {
  if (!authorized()) return;
  settingsClearWifi();
  server.send(200, "text/plain; charset=utf-8", "AP mode, reboot");
  delay(300);
  ESP.restart();
}

#include <Update.h>

static void handleUpdatePost() {
  if (!authorized()) return;
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain; charset=utf-8", (Update.hasError()) ? "Ошибка OTA!" : "Успешно! Перезагрузка...");
  delay(500);
  ESP.restart();
}

static void handleUpdateUpload() {
  if (!authorized()) return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    displayMessage("OTA обновление", "прошивка...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      displayMessage("OTA завершено", "перезагрузка");
    } else {
      displayMessage("OTA ошибка", "отмена");
    }
  }
}

static bool httpStarted = false;

static void handleApiStatus() {
  if (!authorized()) return;
  String s = "{";
  s += "\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? "connected" : "disconnected") + "\",";
  s += "\"ssid\":\"" + htmlEscape(WiFi.SSID()) + "\",";
  s += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  s += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  s += "\"city\":\"" + htmlEscape(settings.city) + "\",";
  s += "\"cityLabel\":\"" + htmlEscape(settings.cityLabel) + "\",";
  s += "\"lat\":" + String(settings.lat, 4) + ",";
  s += "\"lon\":" + String(settings.lon, 4) + ",";
  s += "\"utcOffset\":" + String(settings.utcOffset) + ",";
  s += "\"timezone\":\"" + htmlEscape(settings.timezone) + "\",";
  s += "\"wx_ok\":" + String(weather.ok ? "true" : "false") + ",";
  s += "\"wx_temp\":" + String(weather.temp, 1) + ",";
  s += "\"wx_hum\":" + String(weather.humidity, 0) + ",";
  s += "\"wx_wind\":" + String(weather.wind, 1) + ",";
  s += "\"wx_code\":" + String(weather.code) + ",";
  s += "\"wx_tmin\":" + String(weather.tmin, 1) + ",";
  s += "\"wx_tmax\":" + String(weather.tmax, 1) + ",";
  s += "\"wx_precip\":" + String(weather.precipProb) + ",";
  s += "\"wx_sunrise\":\"" + weather.sunrise + "\",";
  s += "\"wx_sunset\":\"" + weather.sunset + "\",";
  s += "\"wx_age\":" + String(weather.fetchedAt > 0 ? (millis() - weather.fetchedAt) / 1000 : -1) + ",";
  s += "\"air_ok\":" + String(air.ok ? "true" : "false") + ",";
  s += "\"air_aqi\":" + String(air.aqi, 0) + ",";
  s += "\"air_pm25\":" + String(air.pm25, 1) + ",";
  s += "\"air_age\":" + String(air.fetchedAt > 0 ? (millis() - air.fetchedAt) / 1000 : -1) + ",";
  s += "\"rates_ok\":" + String(rates.ok ? "true" : "false") + ",";
  s += "\"rates_usd\":" + String(rates.usd, 2) + ",";
  s += "\"rates_eur\":" + String(rates.eur, 2) + ",";
  s += "\"rates_cny\":" + String(rates.cny, 2) + ",";
  s += "\"rates_date\":\"" + rates.date + "\",";
  s += "\"rates_age\":" + String(rates.fetchedAt > 0 ? (millis() - rates.fetchedAt) / 1000 : -1) + ",";
  s += "\"weatherError\":\"" + htmlEscape(weatherError) + "\",";
  s += "\"airError\":\"" + htmlEscape(airError) + "\",";
  s += "\"ratesError\":\"" + htmlEscape(ratesError) + "\",";
  s += "\"geoError\":\"" + htmlEscape(geoError) + "\",";
  s += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  s += "\"lastError\":\"" + htmlEscape(lastError) + "\",";
  s += "\"uptime\":" + String(millis() / 1000);
  s += "}";
  server.send(200, "application/json", s);
}

static void handleDiag() {
  if (!authorized()) return;
  String html = F("<!DOCTYPE html><html lang=ru><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Desk OLED — Диагностика</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font:14px/1.5 system-ui,sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh}"
    "h1{font-size:1.1rem;padding:16px;border-bottom:1px solid #21262d;color:#58a6ff}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px;padding:16px}"
    ".card{background:#161b22;border:1px solid #21262d;border-radius:10px;padding:14px}"
    ".card h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:#8b949e;margin-bottom:10px}"
    ".row{display:flex;justify-content:space-between;align-items:center;padding:4px 0;border-bottom:1px solid #21262d0a}"
    ".row:last-child{border-bottom:none}"
    ".key{color:#8b949e;font-size:.85rem}"
    ".val{font-family:monospace;font-size:.9rem;text-align:right}"
    ".ok{color:#3fb950}.err{color:#f85149}.warn{color:#e3b341}"
    ".badge{display:inline-block;padding:2px 8px;border-radius:9999px;font-size:.75rem;font-weight:600}"
    ".badge.ok{background:#1f3d2a;color:#3fb950}"
    ".badge.err{background:#3d1f1f;color:#f85149}"
    "button{padding:8px 16px;border-radius:6px;border:none;cursor:pointer;font-size:.85rem;font-weight:600}"
    ".btn-fetch{background:#1f6feb;color:#fff}"
    ".btn-reload{background:#21262d;color:#c9d1d9}"
    ".actions{display:flex;gap:8px;padding:12px 16px}"
    "#status-bar{padding:8px 16px;font-size:.75rem;color:#8b949e;border-top:1px solid #21262d}"
    "</style></head><body>"
    "<h1>\xF0\x9F\x94\x8D ESP32-C3 Desk OLED &mdash; Диагностика</h1>"
    "<div class=actions>"
    "<button class=btn-fetch onclick=forceFetch()>&#9654; Запросить данные сейчас</button>"
    "<button class=btn-reload onclick=load()>&#8635; Обновить</button>"
    "<a href=/ style='padding:8px 16px;background:#21262d;color:#c9d1d9;border-radius:6px;text-decoration:none;font-size:.85rem'>&#8592; Настройки</a>"
    "</div>"
    "<div class=grid id=grid><p style='padding:16px;color:#8b949e'>Загрузка...</p></div>"
    "<div id=status-bar>Авто-обновление каждые 3 сек</div>"
    "<script>"
    "function badge(ok){return '<span class=badge'+' '+(ok?'ok':'err')+'>'+(ok?'OK':'ERR')+'</span>'}"
    "function age(s){if(s<0)return'—';if(s<60)return s+'с';return Math.floor(s/60)+'м '+s%60+'с'}"
    "function row(k,v){return'<div class=row><span class=key>'+k+'</span><span class=val>'+v+'</span></div>'}"
    "async function load(){"
    "  try{"
    "    const r=await fetch('/api/status');"
    "    const d=await r.json();"
    "    document.getElementById('status-bar').textContent='Обновлено: '+new Date().toLocaleTimeString();"
    "    let g='';"
    "    g+='<div class=card><h2>&#128246; Сеть</h2>';"
    "    g+=row('Wi-Fi',badge(d.wifi==='connected')+' '+d.ssid);"
    "    g+=row('IP адрес','<span class='+(d.ip&&d.ip!=='0.0.0.0'?'ok':'err')+'>'+d.ip+'</span>');"
    "    g+=row('RSSI',d.rssi+'&nbsp;dBm');"
    "    g+=row('Uptime',age(d.uptime));"
    "    g+='</div>';"
    "    g+='<div class=card><h2>&#128205; Геолокация</h2>';"
    "    g+=row('Город',d.cityLabel||d.city);"
    "    g+=row('Широта','<span class='+(d.lat!==0?'ok':'err')+'>'+d.lat+'</span>');"
    "    g+=row('Долгота','<span class='+(d.lon!==0?'ok':'err')+'>'+d.lon+'</span>');"
    "    g+=row('UTC offset',d.utcOffset+'s (UTC+'+(d.utcOffset/3600)+'h)');"
    "    g+='</div>';"
    "    g+='<div class=card><h2>&#9925; Погода</h2>';"
    "    g+=row('Статус',badge(d.wx_ok));"
    "    if(d.wx_ok){"
    "      g+=row('Температура',d.wx_temp+'°C');"
    "      g+=row('Влажность',d.wx_hum+'%');"
    "      g+=row('Ветер',d.wx_wind+'&nbsp;м/с');"
    "      g+=row('WMO код',d.wx_code);"
    "      g+=row('Мин/Макс',d.wx_tmin+'° / '+d.wx_tmax+'°');"
    "      g+=row('Осадки',d.wx_precip+'%');"
    "      g+=row('Восход',d.wx_sunrise||'—');"
    "      g+=row('Закат',d.wx_sunset||'—');"
    "      g+=row('Данные получены',age(d.wx_age)+' назад');"
    "    }"
    "    g+='</div>';"
    "    g+='<div class=card><h2>&#129481; Качество воздуха</h2>';"
    "    g+=row('Статус',badge(d.air_ok));"
    "    if(d.air_ok){"
    "      g+=row('AQI',d.air_aqi);"
    "      g+=row('PM2.5',d.air_pm25+' мкг/м³');"
    "      g+=row('Данные получены',age(d.air_age)+' назад');"
    "    }"
    "    g+='</div>';"
    "    g+='<div class=card><h2>&#128176; Курсы ЦБ</h2>';"
    "    g+=row('Статус',badge(d.rates_ok));"
    "    if(d.rates_ok){"
    "      g+=row('USD',d.rates_usd+'&nbsp;₽');"
    "      g+=row('EUR',d.rates_eur+'&nbsp;₽');"
    "      g+=row('CNY',d.rates_cny+'&nbsp;₽');"
    "      g+=row('Дата',d.rates_date);"
    "      g+=row('Данные получены',age(d.rates_age)+' назад');"
    "    }"
    "    g+='</div>';"
    "    g+='<div class=card><h2>&#9888;&#65039; Последняя ошибка</h2>';"
    "    g+=row('Error','<span class='+(d.lastError?'err':'ok')+'>'+((d.lastError&&d.lastError.length)?d.lastError:'нет')+'</span>');"
    "    g+='</div>';"
    "    document.getElementById('grid').innerHTML=g;"
    "  }catch(e){"
    "    document.getElementById('status-bar').textContent='Ошибка: '+e;"
    "  }"
    "}"
    "async function forceFetch(){"
    "  document.getElementById('status-bar').textContent='Запрашиваем данные...';"
    "  await fetch('/api/fetch');"
    "  setTimeout(load,3000);"
    "}"
    "load();"
    "setInterval(load,3000);"
    "</script></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

static void handleApiFetch() {
  if (!authorized()) return;
  fetchAllData(true);
  handleApiStatus();
}

static void registerRoutes() {
  if (httpStarted) return;
  httpStarted = true;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/rescan", HTTP_POST, handleRescan);
  server.on("/ap", HTTP_POST, handleForceAp);
  server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/fetch", HTTP_GET, handleApiFetch);
  server.on("/diag", HTTP_GET, handleDiag);
  server.on("/generate_204", handleRoot);
  server.on("/gen_204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/canonical.html", handleRoot);
  server.on("/ncsi.txt", handleRoot);
  server.on("/connecttest.txt", handleRoot);
  server.on("/redirect", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void portalBeginAp() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "DeskC3-%02X%02X", mac[4], mac[5]);
  apSsid = ssid;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid.c_str(), AP_PASS);
  delay(200);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  WiFi.scanNetworks();
  registerRoutes();
}

void portalBeginSta() {
  apActive = false;
  registerRoutes();
}

void portalStartConfigAp() {
  portalBeginAp();
}

void portalLoop() {
  if (apActive) dns.processNextRequest();
  server.handleClient();
}
