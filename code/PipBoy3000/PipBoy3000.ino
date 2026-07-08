// PipBoy 3000 — ESP32 + 4" TFT + DFPlayer Mini + SHT31
//
// Unified sketch: replaces the four Thingiverse variants
// (Celsius/Fahrenheit x plain/WEBPORTAL) with one configurable build.
// Based on Fallout_PipBoy3000_Fahrenheit_WEBPORTAL.ino by jeje95 / Jéjé l'Ingé
// (original project: https://www.thingiverse.com/thing:6654866,
// code: https://github.com/jejelinge/PIPBOY_3000), original GIF player
// adapted by Bodmer for TFT_eSPI.
//
// Fixes applied 2026-07-08 (see README.md in the fork folder for the full list):
//  - single sketch, USE_FAHRENHEIT toggle instead of duplicate files
//  - native ESP32 SNTP time (configTzTime + POSIX TZ) replaces NTPClient
//    and the hand-rolled DST window; correct US DST, no unsafe time_t cast
//  - midnight no longer shows "Afternoon"
//  - DFPlayer event pump wired up (SD-removed / file-error toasts now work)
//  - randomSeed() so radio/button sounds differ between boots
//  - toast no longer permanently erases the footer bitmaps
//  - portable include paths, dead code removed

//========================USEFUL VARIABLES=============================
#define USE_FAHRENHEIT                 // comment out for Celsius
// POSIX TZ string — DST is handled automatically. US Central shown;
// examples: "EST5EDT,M3.2.0,M11.1.0" "MST7MDT,M3.2.0,M11.1.0"
// "PST8PDT,M3.2.0,M11.1.0" "CET-1CEST,M3.5.0,M10.5.0/3" (France)
#define TZ_INFO "CST6CDT,M3.2.0,M11.1.0"
uint16_t notification_volume = 25;     // 0-30 (30 distorts and can brown out)
//=====================================================================
bool res;

// Load GIF library
#include <AnimatedGIF.h>
AnimatedGIF gif;

#include "images/INIT.h"      // boot animation (INIT3.h is a smaller alternate; include only one — both define INIT[])
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

#define IN_STAT 25
#define IN_INV 26
#define IN_DATA 27
#define IN_TIME 32
#define IN_RADIO 33

#define REPEAT_CAL false
#define Light_green  0x35C2
#define Dark_green   0x0261
#define Time_color   0x04C0
#define Amber_warn   0xFDA0  // Orange/amber for warnings
#define Red_error    0xF800  // Red for errors

#include "WiFiManager.h"
#include "DFRobotDFPlayerMini.h"
#include "FS.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include "Adafruit_SHT31.h"
#include <time.h>

const byte RXD2 = 16;
const byte TXD2 = 17;

DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);
#define FPSerial Serial1
TFT_eSPI tft = TFT_eSPI();
Adafruit_SHT31 sht31 = Adafruit_SHT31();

int flag = 0;
int prev_hour = -1;
String localip;
struct tm timeinfo;

// Track subsystem status for debug
bool dfPlayerOK = false;
bool sensorOK = false;
bool wifiOK = false;
bool ntpOK = false;

byte omm = 99, oss = 99;
byte xcolon = 0, xsecs = 0;

// ============================================================
//  FALLOUT-THEMED BOOT TERMINAL ON TFT
// ============================================================

int bootLine = 0;           // Current Y position for boot messages
const int BOOT_LINE_H = 22; // Line height in pixels
const int BOOT_X = 8;       // Left margin
const int BOOT_START_Y = 5; // Top margin

void bootClear() {
  tft.fillScreen(TFT_BLACK);
  bootLine = BOOT_START_Y;
}

// Print a boot line with status indicator
// status: 0 = info (green), 1 = OK (green), 2 = WARN (amber), 3 = ERROR (red), 4 = header
void bootPrint(const char* msg, uint8_t status = 0) {
  if (bootLine > 290) {  // Screen full, scroll up visually
    bootClear();
  }

  uint16_t color;
  const char* prefix;
  switch (status) {
    case 1:  color = Light_green; prefix = "[  OK  ] "; break;
    case 2:  color = Amber_warn;  prefix = "[ WARN ] "; break;
    case 3:  color = Red_error;   prefix = "[ FAIL ] "; break;
    case 4:  // Header / title
      tft.setTextColor(Light_green, TFT_BLACK);
      tft.drawString(msg, BOOT_X, bootLine, 2);
      bootLine += BOOT_LINE_H + 4;
      return;
    default: color = Dark_green;  prefix = "  > "; break;
  }

  tft.setTextColor(color, TFT_BLACK);
  String line = String(prefix) + String(msg);
  tft.drawString(line, BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H;

  // Also print to Serial
  Serial.println(line);
}

// Draws the decorative header
void bootHeader() {
  bootClear();
  tft.setTextColor(Light_green, TFT_BLACK);

  // PipBoy terminal header
  tft.drawString("ROBCO INDUSTRIES (TM) TERMLINK", BOOT_X, BOOT_START_Y, 2);
  bootLine = BOOT_START_Y + BOOT_LINE_H;
  tft.drawString("PIPBOY 3000 MARK IV OS v4.1.7", BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H;

  // Decorative separator
  tft.drawString("================================", BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H + 4;
}

// Show a blinking cursor effect (brief)
void bootCursor(int count = 3) {
  for (int i = 0; i < count; i++) {
    tft.fillRect(BOOT_X, bootLine, 10, 14, Light_green);
    delay(150);
    tft.fillRect(BOOT_X, bootLine, 10, 14, TFT_BLACK);
    delay(100);
  }
}

// Show final status summary before launching main app
void bootSummary() {
  bootLine += 6;
  tft.drawLine(BOOT_X, bootLine, 470, bootLine, Dark_green);
  bootLine += 8;

  tft.setTextColor(Light_green, TFT_BLACK);
  String summary = "SYS: WiFi[";
  summary += wifiOK   ? "OK" : "!!";
  summary += "] NTP[";
  summary += ntpOK    ? "OK" : "!!";
  summary += "] AUD[";
  summary += dfPlayerOK ? "OK" : "!!";
  summary += "] SENS[";
  summary += sensorOK ? "OK" : "!!";
  summary += "]";
  tft.drawString(summary, BOOT_X, bootLine, 2);
  bootLine += BOOT_LINE_H;

  if (wifiOK && dfPlayerOK && sensorOK) {
    bootPrint("ALL SYSTEMS NOMINAL", 1);
  } else {
    bootPrint("BOOT WITH WARNINGS - CHECK LOG", 2);
  }
  delay(1500);
}

// ============================================================
//  FALLOUT CSS FOR WIFIMANAGER CAPTIVE PORTAL
// ============================================================

const char PIPBOY_CSS[] PROGMEM = R"rawliteral(
<style>
  :root {
    --pip-green: #30ff50;
    --pip-dark: #0a3c12;
    --pip-bg: #0b1a0e;
    --pip-border: #1a5c2a;
    --pip-glow: rgba(48, 255, 80, 0.15);
    --pip-dim: #186828;
  }

  * { box-sizing: border-box; }

  body {
    background-color: var(--pip-bg) !important;
    color: var(--pip-green) !important;
    font-family: 'Share Tech Mono', 'Courier New', monospace !important;
    margin: 0; padding: 20px;
    min-height: 100vh;
    background-image:
      repeating-linear-gradient(
        0deg,
        transparent,
        transparent 2px,
        rgba(0,0,0,0.15) 2px,
        rgba(0,0,0,0.15) 4px
      );
    animation: flicker 0.15s infinite alternate;
  }

  @keyframes flicker {
    0%   { opacity: 0.97; }
    100% { opacity: 1; }
  }

  /* Scanline overlay */
  body::after {
    content: '';
    position: fixed; top: 0; left: 0;
    width: 100%; height: 100%;
    pointer-events: none;
    background: repeating-linear-gradient(
      0deg,
      rgba(0,0,0,0) 0px,
      rgba(0,0,0,0) 1px,
      rgba(0,0,0,0.1) 1px,
      rgba(0,0,0,0.1) 2px
    );
    z-index: 9999;
  }

  .wrap {
    max-width: 480px !important;
    margin: 0 auto;
    padding: 20px;
    border: 1px solid var(--pip-border);
    background: rgba(10, 60, 18, 0.3);
    box-shadow: 0 0 30px var(--pip-glow), inset 0 0 30px rgba(0,0,0,0.5);
  }

  /* Header area */
  div[style*='text-align:left'], .msg {
    border-bottom: 1px solid var(--pip-dim);
    padding-bottom: 12px;
    margin-bottom: 16px;
  }

  h1, h2, h3 {
    color: var(--pip-green) !important;
    text-shadow: 0 0 10px var(--pip-green), 0 0 20px rgba(48,255,80,0.3);
    font-weight: normal !important;
    letter-spacing: 2px;
    text-transform: uppercase;
  }

  /* Input fields */
  input[type="text"],
  input[type="password"],
  select {
    background: var(--pip-dark) !important;
    color: var(--pip-green) !important;
    border: 1px solid var(--pip-dim) !important;
    font-family: 'Share Tech Mono', monospace !important;
    font-size: 16px !important;
    padding: 10px 12px !important;
    width: 100% !important;
    outline: none !important;
    border-radius: 0 !important;
    transition: border-color 0.2s, box-shadow 0.2s;
  }

  input:focus, select:focus {
    border-color: var(--pip-green) !important;
    box-shadow: 0 0 8px var(--pip-glow) !important;
  }

  input::placeholder {
    color: var(--pip-dim) !important;
    opacity: 1;
  }

  /* Buttons */
  button, input[type="submit"], .D {
    background: transparent !important;
    color: var(--pip-green) !important;
    border: 1px solid var(--pip-green) !important;
    font-family: 'Share Tech Mono', monospace !important;
    font-size: 15px !important;
    padding: 12px 20px !important;
    cursor: pointer;
    text-transform: uppercase;
    letter-spacing: 2px;
    width: 100% !important;
    margin: 6px 0 !important;
    transition: all 0.15s;
    border-radius: 0 !important;
    text-decoration: none !important;
    display: block !important;
    text-align: center !important;
  }

  button:hover, input[type="submit"]:hover, .D:hover {
    background: var(--pip-green) !important;
    color: var(--pip-bg) !important;
    box-shadow: 0 0 15px var(--pip-glow);
    text-shadow: none;
  }

  button:active, input[type="submit"]:active {
    transform: scale(0.98);
  }

  /* Links */
  a {
    color: var(--pip-green) !important;
    text-decoration: none !important;
  }
  a:hover { text-decoration: underline !important; }

  /* Labels */
  label { color: var(--pip-dim) !important; font-size: 13px; text-transform: uppercase; letter-spacing: 1px; }

  /* Quality bars */
  .q {
    color: var(--pip-dim) !important;
  }

  /* Footer / info text */
  .c, .msg, small {
    color: var(--pip-dim) !important;
    font-size: 12px;
  }

  /* Custom branding header */
  #pipboy-brand {
    text-align: center;
    padding: 15px 0 10px 0;
    border-bottom: 2px solid var(--pip-dim);
    margin-bottom: 16px;
  }
  #pipboy-brand h2 {
    margin: 0 0 4px 0;
    font-size: 20px;
  }
  #pipboy-brand .sub {
    color: var(--pip-dim);
    font-size: 11px;
    letter-spacing: 3px;
  }
</style>

<div id="pipboy-brand">
  <h2>// PIPBOY 3000 //</h2>
  <div class="sub">ROBCO INDUSTRIES UNIFIED OS</div>
  <div class="sub">NETWORK CONFIGURATION MODULE</div>
</div>
)rawliteral";

// ============================================================
//  SETUP
// ============================================================

void setup() {

  pinMode(IN_RADIO, INPUT_PULLUP);
  pinMode(IN_STAT, INPUT_PULLUP);
  pinMode(IN_DATA, INPUT_PULLUP);
  pinMode(IN_INV, INPUT_PULLUP);
  pinMode(IN_TIME, INPUT_PULLUP);

  Serial.begin(115200);
  randomSeed(esp_random());   // otherwise radio/button sounds repeat the same sequence every boot

  tft.begin();
  tft.setRotation(1);

  // ---- BOOT SEQUENCE ----
  bootHeader();
  delay(400);

  bootPrint("Initializing subsystems...");
  bootCursor(2);
  delay(200);

  // ---- WiFi Setup ----
  bootPrint("Loading WiFi module...");

  WiFiManager manager;
  manager.setConfigPortalTimeout(120);

  // Inject Fallout CSS into captive portal
  manager.setCustomHeadElement(PIPBOY_CSS);
  manager.setTitle("PIPBOY 3000");
  manager.setClass("invert");   // dark theme base

  // Show step-by-step portal instructions on TFT
  bootPrint("Searching for saved network...");
  bootCursor(2);
  bootLine += 4;
  bootPrint("WIFI SETUP INSTRUCTIONS:", 4);
  bootPrint("1. Open WiFi on your phone", 0);
  bootPrint("2. Connect to: PIPBOY3000", 0);
  bootPrint("3. Password:   password", 0);
  bootPrint("4. Open 192.168.4.1 in browser", 0);
  bootPrint("5. Select your WiFi network", 0);
  bootLine += 4;
  bootPrint("Waiting for connection...", 2);

  res = manager.autoConnect("PIPBOY3000", "password");

  if (!res) {
    wifiOK = false;
    bootPrint("WiFi connection FAILED", 3);
    bootPrint("Timeout after 120s", 3);
    bootLine += 6;
    bootPrint("Check SSID and PASSWORD", 2);
    bootPrint("Rebooting in 6 seconds...", 2);
    delay(6000);
    bootPrint(">>> ESP.restart()", 0);
    delay(1000);
    ESP.restart();
  }

  wifiOK = true;
  localip = WiFi.localIP().toString();
  bootPrint("WiFi connected", 1);

  String ipMsg = "IP: " + localip;
  bootPrint(ipMsg.c_str(), 0);
  delay(300);

  // ---- Time via SNTP (DST handled by the TZ rule) ----
  bootPrint("Syncing clock via NTP...");
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");

  ntpOK = getLocalTime(&timeinfo, 10000);   // wait up to 10 s for first sync
  if (ntpOK) {
    bootPrint("NTP time synced", 1);
  } else {
    bootPrint("NTP unavailable - time may drift", 2);
  }
  delay(200);

  // ---- DFPlayer Mini ----
  bootPrint("Init DFPlayer serial...");
  FPSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(1000);

  bootPrint("Connecting DFPlayer Mini...");
  if (!myDFPlayer.begin(FPSerial, true, true)) {
    dfPlayerOK = false;
    bootPrint("DFPlayer OFFLINE", 3);
    bootPrint("1. Check wiring RX/TX", 2);
    bootPrint("2. Insert SD card (FAT32)", 2);
    bootPrint("Audio disabled - continuing...", 2);
    delay(2000);
    // NOTE: Graceful degradation instead of blocking forever
  } else {
    dfPlayerOK = true;
    myDFPlayer.volume(notification_volume);
    myDFPlayer.setTimeOut(500);
    bootPrint("DFPlayer online", 1);

    String volMsg = "Volume: " + String(notification_volume) + "/30";
    bootPrint(volMsg.c_str(), 0);
  }
  delay(200);

  // ---- SHT31 Temperature Sensor ----
  bootPrint("Scanning I2C for SHT31...");
  if (!sht31.begin(0x44)) {
    sensorOK = false;
    bootPrint("SHT31 NOT FOUND (0x44)", 3);
    bootPrint("Check I2C wiring SDA/SCL", 2);
    bootPrint("Temp/humidity disabled", 2);
    delay(2000);
    // Graceful degradation instead of blocking
  } else {
    sensorOK = true;
    bootPrint("SHT31 sensor online", 1);

    if (sht31.isHeaterEnabled()) {
      bootPrint("Heater: ENABLED", 0);
    } else {
      bootPrint("Heater: DISABLED", 0);
    }
  }
  delay(200);

  // ---- Boot Summary ----
  bootSummary();

  // ---- GIF Engine ----
  gif.begin(BIG_ENDIAN_PIXELS);

  // ---- Play startup sound & animation ----
  if (dfPlayerOK) {
    myDFPlayer.playMp3Folder(1);
  }

  delay(500);

  if (gif.open((uint8_t *)INIT, sizeof(INIT), GIFDraw)) {
    tft.startWrite();
    while (gif.playFrame(true, NULL)) {
      yield();
    }
    gif.close();
    tft.endWrite();
  }
}

// ============================================================
//  TFT ERROR OVERLAY - Show errors during runtime
// ============================================================

bool toastVisible = false;

// Display a temporary error/warning toast at bottom of screen
void tftToast(const char* msg, uint8_t level = 2) {
  uint16_t color = (level == 3) ? Red_error : Amber_warn;
  uint16_t bgColor = TFT_BLACK;

  // Draw toast bar at bottom
  tft.fillRect(0, 290, 480, 30, bgColor);
  tft.drawRect(0, 290, 480, 30, color);
  tft.setTextColor(color, bgColor);
  tft.drawString(msg, 8, 296, 2);
  toastVisible = true;
}

// Clear the toast area and restore the footer it covered
void tftToastClear() {
  if (!toastVisible) return;
  tft.fillRect(0, 290, 480, 30, TFT_BLACK);
  drawFooter();
  toastVisible = false;
}

// Footer bitmaps shared by the DATA / TIME / RADIO screens
void drawFooter() {
  tft.drawBitmap(35, 300, Bottom_layer_2Bottom_layer_2, 380, 22, Dark_green);
  tft.drawBitmap(35, 300, myBitmapDate, 380, 22, Light_green);
}

// Full-screen error with Fallout terminal style
void tftErrorScreen(const char* title, const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
  tft.fillScreen(TFT_BLACK);

  // Red border
  tft.drawRect(2, 2, 476, 316, Red_error);
  tft.drawRect(3, 3, 474, 314, Red_error);

  // Header
  tft.setTextColor(Red_error, TFT_BLACK);
  tft.drawString("!!! SYSTEM ERROR !!!", 120, 15, 2);

  // Separator
  tft.drawLine(10, 40, 470, 40, Red_error);

  // Error title
  tft.setTextColor(Amber_warn, TFT_BLACK);
  tft.drawString(title, 20, 55, 4);

  // Details
  tft.setTextColor(Light_green, TFT_BLACK);
  int y = 100;
  if (line1) { tft.drawString(line1, 20, y, 2); y += 25; }
  if (line2) { tft.drawString(line2, 20, y, 2); y += 25; }
  if (line3) { tft.drawString(line3, 20, y, 2); y += 25; }

  // Footer
  tft.setTextColor(Dark_green, TFT_BLACK);
  tft.drawString("ROBCO INDUSTRIES TERMLINK", 130, 280, 2);
}

// ============================================================
//  PLAY SOUND (safe - checks dfPlayerOK)
// ============================================================

void playSound(int folder_min, int folder_max) {
  if (dfPlayerOK) {
    myDFPlayer.playMp3Folder(random(folder_min, folder_max));
  }
}

// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {

  getLocalTime(&timeinfo, 50);

  // ---- DFPlayer event pump (SD removed, file errors, track finished) ----
  if (myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read());
  }

  // ---- STAT Button ----
  if (digitalRead(IN_STAT) == false) {
    flag = 1;
    playSound(2, 5);
    while (digitalRead(IN_STAT) == false) {
      if (gif.open((uint8_t *)STAT, sizeof(STAT), GIFDraw)) {
        tft.startWrite();
        while (gif.playFrame(true, NULL)) { yield(); }
        gif.close();
        tft.endWrite();
      }
    }
  }

  // ---- INV Button ----
  if (digitalRead(IN_INV) == false) {
    flag = 1;
    playSound(2, 5);
    while (digitalRead(IN_INV) == false) {
      if (gif.open((uint8_t *)INV, sizeof(INV), GIFDraw)) {
        tft.startWrite();
        while (gif.playFrame(true, NULL)) { yield(); }
        gif.close();
        tft.endWrite();
      }
    }
  }

  // ---- DATA Button (Temperature/Humidity) ----
  if (digitalRead(IN_DATA) == false) {
    flag = 1;
    playSound(2, 5);
    tft.fillScreen(TFT_BLACK);
    drawFooter();
#ifdef USE_FAHRENHEIT
    tft.drawBitmap(35, 80, temperatureTemp_hum_F, 408, 29, Light_green);
#else
    tft.drawBitmap(35, 80, temperatureTemp_humTemp_hum_2, 408, 29, Light_green);
#endif
    tft.drawBitmap(200, 200, RadiationRadiation, 62, 61, Light_green);

    while (digitalRead(IN_DATA) == false) {

      if (!sensorOK) {
        // Show error toast if sensor is offline
        tftToast("SENSOR OFFLINE - NO DATA", 3);
      } else {
        float t = sht31.readTemperature();
        float h = sht31.readHumidity();

        if (isnan(t) || isnan(h)) {
          tftToast("SENSOR READ ERROR", 3);
        } else {
          tftToastClear(); // Clear any previous error
          tft.setTextColor(Time_color, TFT_BLACK);
#ifdef USE_FAHRENHEIT
          tft.drawFloat((t * 1.8) + 32, 2, 60, 135, 7);
#else
          tft.drawFloat(t, 2, 60, 135, 7);
#endif
          tft.drawFloat(h, 2, 258, 135, 7);
        }
      }

      if (gif.open((uint8_t *)DATA_1, sizeof(DATA_1), GIFDraw)) {
        tft.startWrite();
        while (gif.playFrame(true, NULL)) { yield(); }
        gif.close();
        tft.endWrite();
      }
    }
  }

  // ---- TIME Button ----
  if (digitalRead(IN_TIME) == false) {
    playSound(2, 5);
    tft.fillScreen(TFT_BLACK);
    drawFooter();

    if (!ntpOK) {
      tftToast("NTP OFFLINE - TIME MAY BE WRONG", 2);
    }

    while (digitalRead(IN_TIME) == false) {
      if (gif.open((uint8_t *)TIME, sizeof(TIME), GIFDraw)) {
        tft.startWrite();
        while (gif.playFrame(true, NULL)) { yield(); }
        gif.close();
        tft.endWrite();
      }
      show_hour();
    }
  }

  // ---- RADIO Button ----
  if (digitalRead(IN_RADIO) == false) {
    flag = 1;

    if (dfPlayerOK) {
      myDFPlayer.playMp3Folder(random(5, 10));
    } else {
      // Show error if audio is offline
      tft.fillScreen(TFT_BLACK);
      tftErrorScreen(
        "AUDIO MODULE",
        "DFPlayer Mini is not responding.",
        "Check SD card and wiring.",
        "Radio function unavailable."
      );
      delay(3000);
    }

    delay(500);
    tft.fillScreen(TFT_BLACK);
    drawFooter();

    if (!dfPlayerOK) {
      tftToast("AUDIO OFFLINE - NO SOUND", 3);
    }

    while (digitalRead(IN_RADIO) == false) {
      if (gif.open((uint8_t *)RADIO, sizeof(RADIO), GIFDraw)) {
        tft.startWrite();
        while (gif.playFrame(true, NULL)) { yield(); }
        gif.close();
        tft.endWrite();
      }
    }
  }

  // ---- Periodic WiFi check (every loop) ----
  if (WiFi.status() != WL_CONNECTED && wifiOK) {
    wifiOK = false;
    Serial.println("[WARN] WiFi connection lost!");
  }
  if (WiFi.status() == WL_CONNECTED && !wifiOK) {
    wifiOK = true;
    Serial.println("[OK] WiFi reconnected.");
  }
}

// ============================================================
//  SHOW HOUR
// ============================================================

void show_hour() {
  getLocalTime(&timeinfo, 50);
  tft.setTextSize(2);
  int hour24 = timeinfo.tm_hour;
  int mm = timeinfo.tm_min;
  int ss = timeinfo.tm_sec;

  int hh = hour24 % 12;
  if (hh == 0) hh = 12;

  if (hour24 != prev_hour) {
    tft.fillRect(140, 210, 200, 50, TFT_BLACK);
  }

  if (hour24 < 12) {
    tft.drawBitmap(150, 220, MorningMorning, 170, 29, Light_green);
  } else {
    tft.drawBitmap(150, 220, afternoonAfternoon, 170, 29, Light_green);
  }

  int xpos = 85;
  int ypos = 90;

  if (omm != mm || flag == 1) {
    omm = mm;
    tft.setTextColor(Time_color, TFT_BLACK);
    if (hh < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
    xpos += tft.drawNumber(hh, xpos, ypos, 7);
    xcolon = xpos;
    xpos += tft.drawChar(':', xpos, ypos - 8, 7);
    if (mm < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
    xpos += tft.drawNumber(mm, xpos, ypos, 7);
    xsecs = xpos;
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
//  DFPLAYER EVENT HANDLER (called from loop via event pump)
// ============================================================

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      tftToast("DFPLAYER: TIMEOUT", 2);
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      dfPlayerOK = true; // Card re-inserted
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      dfPlayerOK = false;
      tftToast("SD CARD REMOVED!", 3);
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      dfPlayerOK = true;
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          tftToast("DFPLAYER: CARD NOT FOUND", 3);
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          tftToast("DFPLAYER: FILE NOT FOUND", 3);
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          tftToast("DFPLAYER: FILE MISMATCH", 3);
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}
