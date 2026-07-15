// PipBoy 3000 — ESP32-S3 firmware (v2)
//
// Original project: https://www.thingiverse.com/thing:6654866 (jeje95)
// Original code:    https://github.com/jejelinge/PIPBOY_3000
// GIF player adapted by Bodmer for TFT_eSPI. All credit for the design,
// models, artwork and original firmware goes to jeje95 / Jéjé l'Ingé.
//
// v2 rework (2026-07-08), targets ESP32-S3 N16R8 — see README.md:
//  - non-blocking screen state machine (5-pos rotary knob), GIFs stepped
//    one frame per loop so web/OTA/LED tasks keep running
//  - always-on PIPBOY3000 hotspot + Fallout-styled control page
//    (volume, radio, LED effects, clock sync from phone, WiFi setup,
//    browser firmware update) — no app needed, works at conventions
//  - DS3231 RTC is the timekeeper; NTP (home WiFi) or the phone button
//    just corrects it. No WiFi = clock still right.
//  - MAX17048 fuel gauge -> HP bar on the STAT screen
//  - LED effects (steady / breath / flicker) via MOSFET on one PWM pin
//  - ArduinoOTA (home WiFi) + /update web OTA (anywhere)
//
// Hardware (ESP32-S3, see README for TFT_eSPI pin setup):
//  rotary 5-pos -> GPIO 4/5/6/7/15 (INPUT_PULLUP, common to GND)
//  DFPlayer     -> Serial1 RX=16 TX=17 (1k series on TX line)
//  I2C SDA=8 SCL=9 -> SHT31 0x44, DS3231 0x68, MAX17048 0x36
//  LED MOSFET gate -> GPIO 21 (LEDC PWM)

//========================USEFUL VARIABLES=============================
#define USE_FAHRENHEIT                 // comment out for Celsius
#define TZ_INFO "CST6CDT,M3.2.0,M11.1.0" // POSIX TZ, DST automatic
// Passwords live in secrets.h (gitignored): copy secrets.h.example to
// secrets.h and set your own. Without it, the PUBLISHED defaults below are
// used — fine on the bench, don't wear it that way.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "secrets.h not found - building with published default passwords"
  #define AP_SSID   "PIPBOY3000"
  #define AP_PASS   "VaultTec2077"     // hotspot WPA2 password (min 8 chars)
  #define OTA_PASS  "vault111"         // ArduinoOTA password
#endif

// Headless bring-up switch. 1 = normal (drives the TFT). 0 = skip ALL display
// hardware — boots to the hotspot + OTA with no panel attached, so a board can
// be flashed and brought up before the 4" TFT is wired / its controller known.
// Override per-build with -DDISPLAY_ENABLED=0; the display config is finalized
// once the panel is wired, then pushed over OTA.
#ifndef DISPLAY_ENABLED
  #define DISPLAY_ENABLED 1
#endif
//=====================================================================

#include <AnimatedGIF.h>
AnimatedGIF gif;

#include "images/INIT.h"      // boot animation (INIT3.h = smaller alternate; both define INIT[])
#include "images/STAT.h"
#include "images/RADIO.h"
#include "images/DATA_1.h"
#include "images/TIME.h"
#include "images/Bottom_layer_2.h"
#include "images/Date.h"
#include "images/INV.h"
#include "images/RADIATION.h"
#include "images/Morning.h"
#include "images/Afternoon.h"
#ifdef USE_FAHRENHEIT
  #include "images/temperatureTemp_hum_F.h"
#else
  #include "images/temperatureTemp_hum.h"
#endif

// ---- Pins (ESP32-S3) ----
#define IN_STAT   4
#define IN_INV    5
#define IN_DATA   6
#define IN_TIME   7
#define IN_RADIO  15
#define LED_PIN   21
#define I2C_SDA   8
#define I2C_SCL   9
const byte RXD2 = 16;
const byte TXD2 = 17;

// ---- Colors ----
#define Light_green  0x35C2
#define Dark_green   0x0261
#define Time_color   0x04C0
#define Amber_warn   0xFDA0
#define Red_error    0xF800

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Wire.h>
#include <SPI.h>
#include "lgfx_pipboy.h"
#include "DFRobotDFPlayerMini.h"
#include "Adafruit_SHT31.h"
#include "RTClib.h"
#include "Adafruit_MAX1704X.h"
#include <time.h>

LGFX tft;
DFRobotDFPlayerMini myDFPlayer;
#define FPSerial Serial1
Adafruit_SHT31 sht31 = Adafruit_SHT31();
RTC_DS3231 rtc;
Adafruit_MAX17048 maxlipo;
WebServer server(80);
Preferences prefs;

// ---- Subsystem status ----
bool dfPlayerOK = false;
bool sensorOK   = false;
bool rtcOK      = false;
bool fuelOK     = false;
bool staOK      = false;   // connected to home WiFi
bool timeOK     = false;   // system clock trustworthy (RTC, NTP or phone)
bool ntpDone    = false;
bool ntpStarted = false;
bool otaStarted = false;

// ---- Persisted settings ----
uint8_t volume    = 25;    // 0-30
uint8_t ledMode   = 1;     // 0 off, 1 steady, 2 breath, 3 flicker
uint8_t ledBright = 180;   // 0-255

// ---- Screen state machine ----
enum Screen { SCR_NONE = -1, SCR_STAT, SCR_INV, SCR_DATA, SCR_TIME, SCR_RADIO };
int currentScreen = SCR_NONE;
bool gifIsOpen = false;
const uint8_t *curGifData = nullptr;
size_t curGifLen = 0;

// clock-draw state
struct tm timeinfo;
byte omm = 99, oss = 99;
byte xcolon = 0;
int flag = 0;
int prev_hour = -1;

unsigned long lastTempDraw = 0;
unsigned long lastBattDraw = 0;
unsigned long lastLedTick  = 0;

// ============================================================
//  BOOT TERMINAL
// ============================================================

int bootLine = 0;
const int BOOT_LINE_H = 22;
const int BOOT_X = 8;
const int BOOT_START_Y = 5;

void bootClear() {
  if (!DISPLAY_ENABLED) return;
  tft.fillScreen(TFT_BLACK);
  bootLine = BOOT_START_Y;
}

// status: 0 info, 1 OK, 2 WARN, 3 FAIL, 4 header
void bootPrint(const char* msg, uint8_t status = 0) {
  uint16_t color;
  const char* prefix;
  switch (status) {
    case 1:  color = Light_green; prefix = "[  OK  ] "; break;
    case 2:  color = Amber_warn;  prefix = "[ WARN ] "; break;
    case 3:  color = Red_error;   prefix = "[ FAIL ] "; break;
    case 4:  prefix = ""; color = Light_green; break;
    default: color = Dark_green;  prefix = "  > "; break;
  }
  String line = String(prefix) + String(msg);
  Serial.println(line);            // serial log always, even headless
  if (!DISPLAY_ENABLED) return;
  if (bootLine > 290) bootClear();
  if (status == 4) {
    tft.setTextColor(Light_green, TFT_BLACK);
    tft.drawString(msg, BOOT_X, bootLine, 2);
    bootLine += BOOT_LINE_H + 4;
    return;
  }
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(line, BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H;
}

void bootHeader() {
  Serial.println("PIPBOY 3000 MARK IV OS v5.0.0 booting...");
  if (!DISPLAY_ENABLED) return;
  bootClear();
  tft.setTextColor(Light_green, TFT_BLACK);
  tft.drawString("ROBCO INDUSTRIES (TM) TERMLINK", BOOT_X, BOOT_START_Y, 2);
  bootLine = BOOT_START_Y + BOOT_LINE_H;
  tft.drawString("PIPBOY 3000 MARK IV OS v5.0.0", BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H;
  tft.drawString("================================", BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H + 4;
}

// ============================================================
//  TOAST + SHARED DRAWING
// ============================================================

bool toastVisible = false;

void drawFooter() {
  if (!DISPLAY_ENABLED) return;
  tft.drawBitmap(35, 300, Bottom_layer_2Bottom_layer_2, 380, 22, Dark_green);
  tft.drawBitmap(35, 300, myBitmapDate, 380, 22, Light_green);
}

void tftToast(const char* msg, uint8_t level = 2) {
  if (!DISPLAY_ENABLED) return;
  uint16_t color = (level == 3) ? Red_error : Amber_warn;
  tft.fillRect(0, 290, 480, 30, TFT_BLACK);
  tft.drawRect(0, 290, 480, 30, color);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(msg, 8, 296, 2);
  toastVisible = true;
}

void tftToastClear() {
  if (!DISPLAY_ENABLED) return;
  if (!toastVisible) return;
  tft.fillRect(0, 290, 480, 30, TFT_BLACK);
  drawFooter();
  toastVisible = false;
}

void playSound(int folder_min, int folder_max) {
  if (dfPlayerOK) myDFPlayer.playMp3Folder(random(folder_min, folder_max));
}

// ============================================================
//  BATTERY HP BAR (STAT screen overlay)
// ============================================================

void drawBattery() {
  if (!DISPLAY_ENABLED) return;
  if (!fuelOK) return;
  float p = maxlipo.cellPercent();
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  const int x = 372, y = 6, w = 100, h = 16;
  tft.setTextColor(Light_green, TFT_BLACK);
  tft.drawString("HP", x - 26, y + 1, 2);
  tft.drawRect(x, y, w, h, Light_green);
  int fill = (int)((w - 4) * p / 100.0f);
  uint16_t barCol = (p < 20) ? Red_error : (p < 40 ? Amber_warn : Light_green);
  tft.fillRect(x + 2, y + 2, fill, h - 4, barCol);
  tft.fillRect(x + 2 + fill, y + 2, (w - 4) - fill, h - 4, TFT_BLACK);
  String pct = String((int)p) + "%  ";
  tft.drawString(pct, x - 26, y + 18, 2);
}

// ============================================================
//  LED EFFECTS (MOSFET on LED_PIN, LEDC PWM)
// ============================================================

void ledTick() {
  unsigned long now = millis();
  if (now - lastLedTick < 30) return;
  lastLedTick = now;
  uint32_t duty = 0;
  switch (ledMode) {
    case 1: duty = ledBright; break;
    case 2: {  // breath
      float s = (sinf(now / 1200.0f) + 1.0f) / 2.0f;      // 0..1
      duty = (uint32_t)(ledBright * (0.15f + 0.85f * s));
      break;
    }
    case 3: {  // flicker (candle-ish random walk)
      static int level = 200;
      level += random(-40, 41);
      if (level < 90) level = 90;
      if (level > 255) level = 255;
      duty = (uint32_t)ledBright * level / 255;
      break;
    }
    default: duty = 0; break;
  }
  ledcWrite(LED_PIN, duty);
}

// ============================================================
//  TIME
// ============================================================

void setSystemTime(uint32_t epochUTC) {
  struct timeval tv = { .tv_sec = (time_t)epochUTC, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  timeOK = true;
  if (rtcOK) rtc.adjust(DateTime(epochUTC));
}

void show_hour() {
  if (!DISPLAY_ENABLED) return;
  if (!getLocalTime(&timeinfo, 20)) return;
  tft.setTextSize(2);
  int hour24 = timeinfo.tm_hour;
  int mm = timeinfo.tm_min;
  int ss = timeinfo.tm_sec;
  int hh = hour24 % 12;
  if (hh == 0) hh = 12;

  if (hour24 != prev_hour) tft.fillRect(140, 210, 200, 50, TFT_BLACK);

  if (hour24 < 12) {
    tft.drawBitmap(150, 220, MorningMorning, 170, 29, Light_green);
  } else {
    tft.drawBitmap(150, 220, afternoonAfternoon, 170, 29, Light_green);
  }

  int xpos = 85;
  const int ypos = 90;

  if (omm != mm || flag == 1) {
    omm = mm;
    tft.setTextColor(Time_color, TFT_BLACK);
    if (hh < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
    xpos += tft.drawNumber(hh, xpos, ypos, 7);
    xcolon = xpos;
    xpos += tft.drawChar(':', xpos, ypos - 8, 7);
    if (mm < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
    xpos += tft.drawNumber(mm, xpos, ypos, 7);
    flag = 0;
  }

  if (oss != ss) {
    oss = ss;
    if (ss % 2) {
      tft.setTextColor(0x39C4, TFT_BLACK);
      tft.drawChar(':', xcolon, ypos - 8, 7);
      tft.setTextColor(Time_color, TFT_BLACK);
    } else {
      tft.setTextColor(Time_color, TFT_BLACK);
      tft.drawChar(':', xcolon, ypos - 8, 7);
    }
  }
  tft.setTextSize(1);
  prev_hour = hour24;
}

// ============================================================
//  DATA SCREEN (temperature / humidity)
// ============================================================

void updateDataReadings() {
  if (!DISPLAY_ENABLED) return;
  if (!sensorOK) {
    tftToast("SENSOR OFFLINE - NO DATA", 3);
    return;
  }
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (isnan(t) || isnan(h)) {
    tftToast("SENSOR READ ERROR", 3);
    return;
  }
  tftToastClear();
  tft.setTextColor(Time_color, TFT_BLACK);
#ifdef USE_FAHRENHEIT
  tft.drawFloat((t * 1.8f) + 32.0f, 2, 60, 135, 7);
#else
  tft.drawFloat(t, 2, 60, 135, 7);
#endif
  tft.drawFloat(h, 2, 258, 135, 7);
}

// ============================================================
//  SCREEN STATE MACHINE
// ============================================================

int readKnob() {
  if (digitalRead(IN_STAT)  == LOW) return SCR_STAT;
  if (digitalRead(IN_INV)   == LOW) return SCR_INV;
  if (digitalRead(IN_DATA)  == LOW) return SCR_DATA;
  if (digitalRead(IN_TIME)  == LOW) return SCR_TIME;
  if (digitalRead(IN_RADIO) == LOW) return SCR_RADIO;
  return SCR_NONE;
}

void closeGif() {
  if (gifIsOpen) {
    gif.close();
    gifIsOpen = false;
  }
}

void setGif(const uint8_t *data, size_t len) {
  closeGif();
  curGifData = data;
  curGifLen = len;
}

void enterScreen(int s) {
  currentScreen = s;
  if (!DISPLAY_ENABLED) { playSound(2, 5); return; }
  closeGif();
  toastVisible = false;
  playSound(2, 5);

  switch (s) {
    case SCR_STAT:
      setGif(STAT, sizeof(STAT));
      lastBattDraw = 0;
      break;
    case SCR_INV:
      setGif(INV, sizeof(INV));
      break;
    case SCR_DATA:
      tft.fillScreen(TFT_BLACK);
      drawFooter();
#ifdef USE_FAHRENHEIT
      tft.drawBitmap(35, 80, temperatureTemp_hum_F, 408, 29, Light_green);
#else
      tft.drawBitmap(35, 80, temperatureTemp_humTemp_hum_2, 408, 29, Light_green);
#endif
      tft.drawBitmap(200, 200, RadiationRadiation, 62, 61, Light_green);
      setGif(DATA_1, sizeof(DATA_1));
      lastTempDraw = 0;
      break;
    case SCR_TIME:
      tft.fillScreen(TFT_BLACK);
      drawFooter();
      if (!timeOK) tftToast("CLOCK NOT SET - SYNC FROM PHONE", 2);
      setGif(TIME, sizeof(TIME));
      flag = 1;
      omm = 99; oss = 99; prev_hour = -1;
      break;
    case SCR_RADIO:
      tft.fillScreen(TFT_BLACK);
      drawFooter();
      if (dfPlayerOK) {
        myDFPlayer.playMp3Folder(random(5, 10));
      } else {
        tftToast("AUDIO OFFLINE - NO SOUND", 3);
      }
      setGif(RADIO, sizeof(RADIO));
      break;
    default:
      break;
  }
}

// Advance the current screen's GIF by one frame; reopen when it ends
void stepGif() {
  if (!DISPLAY_ENABLED) return;
  if (!curGifData) return;
  if (!gifIsOpen) {
    if (!gif.open((uint8_t *)curGifData, curGifLen, GIFDraw)) return;
    gifIsOpen = true;
    tft.startWrite();
  }
  if (!gif.playFrame(true, NULL)) {   // frame delay handled inside
    tft.endWrite();
    gif.close();
    gifIsOpen = false;                // reopened (=looped) next pass
  }
}

void screenOverlays() {
  if (!DISPLAY_ENABLED) return;
  unsigned long now = millis();
  switch (currentScreen) {
    case SCR_TIME:
      show_hour();
      break;
    case SCR_DATA:
      if (now - lastTempDraw > 2000) {
        lastTempDraw = now;
        updateDataReadings();
      }
      break;
    case SCR_STAT:
      if (now - lastBattDraw > 5000) {
        lastBattDraw = now;
        drawBattery();
      }
      break;
    default:
      break;
  }
}

// ============================================================
//  CONTROL PAGE (served on the hotspot and on home WiFi)
// ============================================================

#include "control_page.h"

void handleStatus() {
  char timebuf[24] = "--:--:--";
  if (timeOK && getLocalTime(&timeinfo, 20)) {
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S %m/%d", &timeinfo);
  }
  float t = NAN, h = NAN;
  if (sensorOK) {
    t = sht31.readTemperature();
    h = sht31.readHumidity();
#ifdef USE_FAHRENHEIT
    if (!isnan(t)) t = t * 1.8f + 32.0f;
#endif
  }
  String json = "{";
  json += "\"audio\":" + String(dfPlayerOK ? "true" : "false");
  json += ",\"sensor\":" + String(sensorOK ? "true" : "false");
  json += ",\"rtc\":" + String(rtcOK ? "true" : "false");
  json += ",\"clock\":" + String(timeOK ? "true" : "false");
  json += ",\"wifi\":" + String(staOK ? "true" : "false");
  json += ",\"time\":\"" + String(timebuf) + "\"";
  json += ",\"batt\":" + String(fuelOK ? (int)maxlipo.cellPercent() : -1);
  json += ",\"volt\":\"" + (fuelOK ? String(maxlipo.cellVoltage(), 2) : String("0")) + "\"";
  json += ",\"temp\":" + (isnan(t) ? String("null") : String(t, 1));
  json += ",\"hum\":" + (isnan(h) ? String("null") : String(h, 1));
  json += ",\"volume\":" + String(volume);
  json += ",\"ledmode\":" + String(ledMode);
  json += ",\"ledbright\":" + String(ledBright);
  json += ",\"ip\":\"" + (staOK ? WiFi.localIP().toString() : String("not connected")) + "\"";
  json += ",\"build\":\"ILI9488 " __DATE__ " " __TIME__ "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", CONTROL_PAGE);
  });
  server.on("/api/status", HTTP_GET, handleStatus);

  server.on("/api/time", HTTP_POST, []() {
    uint32_t epoch = (uint32_t)server.arg("epoch").toInt();
    if (epoch > 1700000000UL) {
      setSystemTime(epoch);
      server.send(200, "text/plain", "clock set");
    } else {
      server.send(400, "text/plain", "bad epoch");
    }
  });

  server.on("/api/volume", HTTP_POST, []() {
    int v = server.arg("v").toInt();
    if (v < 0) v = 0;
    if (v > 30) v = 30;
    volume = v;
    prefs.putUChar("vol", volume);
    if (dfPlayerOK) myDFPlayer.volume(volume);
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/play", HTTP_POST, []() {
    int n = server.arg("n").toInt();
    if (dfPlayerOK && n >= 1 && n <= 9) {
      myDFPlayer.playMp3Folder(n);
      server.send(200, "text/plain", "ok");
    } else {
      server.send(503, "text/plain", "audio offline");
    }
  });

  server.on("/api/stop", HTTP_POST, []() {
    if (dfPlayerOK) myDFPlayer.stop();
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/led", HTTP_POST, []() {
    ledMode = (uint8_t)constrain(server.arg("mode").toInt(), 0, 3);
    ledBright = (uint8_t)constrain(server.arg("bright").toInt(), 0, 255);
    prefs.putUChar("ledmode", ledMode);
    prefs.putUChar("ledbright", ledBright);
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/wifi", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0) {
      server.send(400, "text/plain", "ssid required");
      return;
    }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    server.send(200, "text/plain", "connecting");
    WiFi.begin(ssid.c_str(), pass.c_str());
  });

  // Browser firmware update (works over the hotspot at a convention)
  server.on("/update", HTTP_POST, []() {
    bool ok = !Update.hasError();
    server.send(200, "text/html",
      ok ? "<h3>UPDATE OK - REBOOTING</h3>" : "<h3>UPDATE FAILED</h3>");
    if (ok) {
      delay(500);
      ESP.restart();
    }
  }, []() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
      Serial.printf("Web OTA: %s\n", up.filename.c_str());
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
      Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      Update.end(true);
      Serial.printf("Web OTA: %u bytes\n", up.totalSize);
    }
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "VAULT-TEC: NO SUCH TERMINAL ENTRY");
  });
  server.begin();
}

// ============================================================
//  WIFI / NTP / OTA background tick
// ============================================================

void wifiTick() {
  bool sta = (WiFi.status() == WL_CONNECTED);
  if (sta && !staOK) {
    staOK = true;
    Serial.print("[OK] home WiFi, IP: ");
    Serial.println(WiFi.localIP());
    if (!otaStarted) {
      ArduinoOTA.setHostname("pipboy3000");
      ArduinoOTA.setPassword(OTA_PASS);
      ArduinoOTA.begin();
      otaStarted = true;
    }
    if (!ntpStarted) {
      configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
      ntpStarted = true;
    }
  }
  if (!sta && staOK) {
    staOK = false;
    Serial.println("[WARN] home WiFi lost");
  }
  // First NTP fix: trust it and correct the RTC
  if (ntpStarted && !ntpDone) {
    time_t now = time(nullptr);
    if (now > 1700000000L) {
      ntpDone = true;
      timeOK = true;
      if (rtcOK) rtc.adjust(DateTime((uint32_t)now));
      Serial.println("[OK] NTP synced, RTC corrected");
    }
  }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  pinMode(IN_STAT, INPUT_PULLUP);
  pinMode(IN_INV, INPUT_PULLUP);
  pinMode(IN_DATA, INPUT_PULLUP);
  pinMode(IN_TIME, INPUT_PULLUP);
  pinMode(IN_RADIO, INPUT_PULLUP);

  Serial.begin(115200);
  randomSeed(esp_random());

  setenv("TZ", TZ_INFO, 1);
  tzset();

  prefs.begin("pipboy", false);
  volume    = prefs.getUChar("vol", 25);
  ledMode   = prefs.getUChar("ledmode", 1);
  ledBright = prefs.getUChar("ledbright", 180);

  ledcAttach(LED_PIN, 5000, 8);
  ledcWrite(LED_PIN, 0);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (DISPLAY_ENABLED) {
    tft.begin();
    tft.setRotation(1);
    tft.setSwapBytes(true);   // GIF palette is byte-reversed (BIG_ENDIAN_PIXELS)
  }

  bootHeader();
  bootPrint("Initializing subsystems...");

  // ---- RTC (the timekeeper) ----
  bootPrint("Probing DS3231 RTC...");
  if (rtc.begin()) {
    rtcOK = true;
    if (rtc.lostPower()) {
      bootPrint("RTC battery dead - clock unset", 2);
    } else {
      struct timeval tv = { .tv_sec = (time_t)rtc.now().unixtime(), .tv_usec = 0 };
      settimeofday(&tv, nullptr);
      timeOK = true;
      bootPrint("RTC online - clock restored", 1);
    }
  } else {
    bootPrint("DS3231 NOT FOUND (0x68)", 3);
    bootPrint("Clock needs NTP or phone sync", 2);
  }

  // ---- Battery fuel gauge ----
  bootPrint("Probing MAX17048 fuel gauge...");
  if (maxlipo.begin()) {
    fuelOK = true;
    String msg = "Battery: " + String((int)maxlipo.cellPercent()) + "%";
    bootPrint(msg.c_str(), 1);
  } else {
    bootPrint("MAX17048 NOT FOUND (0x36)", 2);
    bootPrint("HP bar disabled", 2);
  }

  // ---- SHT31 ----
  bootPrint("Probing SHT31 sensor...");
  if (sht31.begin(0x44)) {
    sensorOK = true;
    bootPrint("SHT31 sensor online", 1);
  } else {
    bootPrint("SHT31 NOT FOUND (0x44)", 3);
    bootPrint("Temp/humidity disabled", 2);
  }

  // ---- DFPlayer ----
  bootPrint("Connecting DFPlayer Mini...");
  FPSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(800);
  if (myDFPlayer.begin(FPSerial, true, true)) {
    dfPlayerOK = true;
    myDFPlayer.volume(volume);
    myDFPlayer.setTimeOut(500);
    bootPrint("DFPlayer online", 1);
  } else {
    bootPrint("DFPlayer OFFLINE - no audio", 3);
  }

  // ---- WiFi: hotspot always on, home WiFi in background ----
  bootPrint("Starting PIPBOY3000 hotspot...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  bootPrint("Hotspot: PIPBOY3000 / 192.168.4.1", 1);

  // Home WiFi: prefer creds saved via the control page (NVS), else fall back
  // to the compiled-in default from secrets.h (WIFI_SSID/WIFI_PASS). Either
  // way the AP stays up and STA auto-reconnects when home is back in range.
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
#ifdef WIFI_SSID
  if (ssid.length() == 0) { ssid = WIFI_SSID; pass = WIFI_PASS; }
#endif
  if (ssid.length() > 0) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    String msg = "Joining " + ssid + " (background)";
    bootPrint(msg.c_str(), 0);
  } else {
    bootPrint("No home WiFi saved - use control page", 2);
  }

  if (MDNS.begin("pipboy")) MDNS.addService("http", "tcp", 80);
  setupWebServer();
  bootPrint("Control page ready", 1);

  // ---- Summary ----
  bootLine += 6;
  String summary = "SYS: AUD[";
  summary += dfPlayerOK ? "OK" : "!!";
  summary += "] SENS[";
  summary += sensorOK ? "OK" : "!!";
  summary += "] RTC[";
  summary += rtcOK ? "OK" : "!!";
  summary += "] BAT[";
  summary += fuelOK ? "OK" : "!!";
  summary += "]";
  Serial.println(summary);
  if (DISPLAY_ENABLED) {
    tft.setTextColor(Light_green, TFT_BLACK);
    tft.drawString(summary, BOOT_X, bootLine, 2);
  }
  delay(1200);

  // ---- Boot animation + sound ----
  if (DISPLAY_ENABLED) {
    gif.begin(BIG_ENDIAN_PIXELS);
    if (dfPlayerOK) myDFPlayer.playMp3Folder(1);
    if (gif.open((uint8_t *)INIT, sizeof(INIT), GIFDraw)) {
      tft.startWrite();
      while (gif.playFrame(true, NULL)) yield();
      gif.close();
      tft.endWrite();
    }
  } else {
    if (dfPlayerOK) myDFPlayer.playMp3Folder(1);
    Serial.println("[headless] DISPLAY_ENABLED=0 - TFT skipped, hotspot+OTA live");
  }

  // land on whatever the knob points at (default STAT)
  int k = readKnob();
  enterScreen(k == SCR_NONE ? SCR_STAT : k);
}

// ============================================================
//  MAIN LOOP — non-blocking
// ============================================================

void loop() {
  server.handleClient();
  if (otaStarted) ArduinoOTA.handle();
  wifiTick();
  ledTick();

  if (dfPlayerOK && myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read());
  }

  int k = readKnob();
  if (k != SCR_NONE && k != currentScreen) {
    enterScreen(k);
  }

  stepGif();
  screenOverlays();
}

// ============================================================
//  DFPLAYER EVENT HANDLER
// ============================================================

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("DFPlayer: Time Out"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("DFPlayer: Card Inserted"));
      dfPlayerOK = true;
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("DFPlayer: Card Removed"));
      dfPlayerOK = false;
      tftToast("SD CARD REMOVED!", 3);
      break;
    case DFPlayerCardOnline:
      dfPlayerOK = true;
      break;
    case DFPlayerPlayFinished:
      Serial.printf("DFPlayer: track %d finished\n", value);
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError: "));
      switch (value) {
        case Busy:
          Serial.println(F("card not found"));
          tftToast("DFPLAYER: CARD NOT FOUND", 3);
          break;
        case FileIndexOut:
        case FileMismatch:
          Serial.println(F("file not found"));
          tftToast("DFPLAYER: FILE NOT FOUND", 3);
          break;
        default:
          Serial.println(value);
          break;
      }
      break;
    default:
      break;
  }
}
