# PipBoy 3000 ESP32 Fork

Working fork of our PipBoy 3000 wearable.

**Original project: "PipBoy 3000 ESP32" by jeje95 —
<https://www.thingiverse.com/thing:6654866>**
(code: [github.com/jejelinge/PIPBOY_3000](https://github.com/jejelinge/PIPBOY_3000)),
licensed CC BY-NC. All credit for the original design, models, artwork, and
firmware goes to jeje95 / Jéjé l'Ingé. The pristine upstream download stays in
the sibling folder `PipBoy 3000 ESP32 - 6654866/` — this fork is where we make
changes.

> **Build status (2026-07-13):** everything works — power, DMZ networking
> (on `192.168.15.60` / `pipboy.empire12.net`), sound + **radio**,
> temp/humidity, NTP clock, 3-LED channel, remote OTA — **except the display**,
> which is stuck solid-grey and awaiting a replacement screen. Full display
> history, what's been ruled out, and next steps are in
> **[`DISPLAY-BRINGUP-LOG.md`](DISPLAY-BRINGUP-LOG.md)** — read that before
> touching the display. The ESP32 was replaced (new MAC, re-onboarded) and the
> firmware migrated from TFT_eSPI to **LovyanGFX** (TFT_eSPI 2.5.43 can't drive
> SPI on the ESP32-S3 + arduino-3.x core). Build with `pio run` (see
> `platformio.ini`).

## Layout

| Path | Contents |
|------|----------|
| `DISPLAY-BRINGUP-LOG.md` | **Display troubleshooting log** — read first for display work |
| `platformio.ini` | Reproducible build (pioarduino S3 toolchain + LovyanGFX) |
| `WIRING.md` / `wiring-diagram.svg` | v2 wiring — text and graphical versions |
| `code/PipBoy3000-S3/` | **v2 firmware (current)** — ESP32-S3, control page, RTC, HP bar, LED FX, OTA |
| `code/PipBoy3000/` | v1 firmware — classic ESP32, unified/fixed port of upstream (kept as fallback) |
| `stl/` | All 15 printable parts + `Tune_and_Geiger_labels.pdf` (`Ritgh_cap` typo fixed → `Right_cap`) |
| `sd-card/mp3/` | UI sounds 0001–0004 for the DFPlayer SD card |
| `reference/photos/` | Upstream build photos |
| `bom.txt` | Bill of materials (extended — see below) |

## v2 firmware — `code/PipBoy3000-S3/` (current)

Targets an **ESP32-S3 N16R8** dev board (16 MB flash / 8 MB PSRAM). Keeps the
DFPlayer Mini, speaker, LEDs, rotary encoder and TFT from the original design;
adds three small I2C boards. Zero changes to the 3D-printed parts.

**What it adds over v1:**

- **Non-blocking screen engine** — GIFs advance one frame per loop, so the web
  server, OTA, LED effects, and DFPlayer events all keep running regardless of
  knob position (v1/upstream sat in a `while` loop forever).
- **`PIPBOY3000` hotspot + phone control page** — always broadcasting,
  WPA2-protected. Join it from any phone, open `http://192.168.4.1` (or
  `http://pipboy.local` on home WiFi): status, volume, radio tracks, LED
  mode/brightness, home-WiFi setup, **"sync clock from this phone"**, and
  browser firmware upload. No app — chosen over BLE because iOS Safari
  can't do Web Bluetooth.
- **Passwords:** copy `code/PipBoy3000-S3/secrets.h.example` to `secrets.h`
  and set your own hotspot (`AP_PASS`) and OTA (`OTA_PASS`) passwords —
  `secrets.h` is gitignored so they stay out of the repo. Building without
  it falls back to the published defaults with a compiler warning.
- **DS3231 RTC is the timekeeper** — clock survives power-off (±2 min/yr).
  NTP (home WiFi, background) or the phone button correct it. No more WiFi
  dependency, no 120 s boot hang, no reboot loop at conventions.
- **MAX17048 fuel gauge → HP bar** on the STAT screen (amber <40%, red <20%).
- **LED effects** — steady / breath / flicker via one logic-level MOSFET on a
  PWM pin, brightness 0–255, settings persist across reboots.
- **OTA two ways** — ArduinoOTA on home WiFi (password `vault111`), or upload
  a `.bin` from the phone control page over the hotspot. No more opening the
  prop to reflash.

**Wiring (ESP32-S3):** rotary 5-pos → GPIO 4/5/6/7/15 (common to GND);
DFPlayer Serial1 RX=16/TX=17 (1 kΩ in the TX line); I2C SDA=8/SCL=9 shared by
SHT31 (0x44), DS3231 (0x68), MAX17048 (0x36); LED MOSFET gate → GPIO 21;
TFT on SPI: MOSI=11, SCLK=12, CS=10, DC=13, RST=14 (configure TFT_eSPI's
`User_Setup.h` to match — ILI9486, 320×480; the exact defines are in the
PlatformIO `build_flags` below).

**Build (verified 2026-07-08 on ops, PlatformIO):** platform
`espressif32@55.3.39` (pioarduino), board `esp32-s3-devkitc-1`,
`board_build.flash_size = 16MB`, partitions `default_16MB.csv` (dual OTA
slots). Result: flash 28%, RAM 24%. Libraries: TFT_eSPI, AnimatedGIF,
DFRobotDFPlayerMini, Adafruit SHT31 + BusIO, RTClib, Adafruit MAX1704X.
TFT_eSPI config (panel is **ILI9486**, confirmed on hardware 2026-07-11) —
build flags, also mirrored in `code/PipBoy3000-S3/TFT_eSPI_Setup_PipBoy.h`
for Arduino-IDE users:
`USER_SETUP_LOADED, ILI9486_DRIVER, USE_HSPI_PORT, TFT_WIDTH=320,
TFT_HEIGHT=480, TFT_MISO=-1, TFT_MOSI=11, TFT_SCLK=12, TFT_CS=10, TFT_DC=13,
TFT_RST=14, LOAD_GLCD, LOAD_FONT2, LOAD_FONT4, LOAD_FONT6, LOAD_FONT7,
LOAD_FONT8, SMOOTH_FONT, SPI_FREQUENCY=20000000`.

**`USE_HSPI_PORT` is load-bearing:** without it, on the ESP32-S3 TFT_eSPI
shares the global `SPI` object by reference and `tft.begin()` faults in
`spi.beginTransaction()` (null bus) → boot-loop. Giving it a dedicated port
fixes it. ILI9486 SPI is happier at 20 MHz than 40 (40 glitched); GIF
playback is a bit slower on ILI9486 (3 bytes/pixel) — bump the clock later
only if it's stable. If colors look inverted add `TFT_INVERSION_ON`; if
red/blue are swapped it's an RGB/BGR panel — add `TFT_RGB_ORDER=TFT_BGR`.

Note: the control-page HTML lives in `control_page.h` — the Arduino `.ino`
preprocessor mangles JS inside raw strings in `.ino` files, so don't fold it
back into the sketch.

### Headless bring-up mode (`DISPLAY_ENABLED`)

The sketch has a compile switch `DISPLAY_ENABLED` (default `1`). Build with
**`-DDISPLAY_ENABLED=0`** to skip ALL TFT hardware — the board boots straight
to the hotspot + OTA with no panel attached and no crash. Use it to flash and
bring a board up **before the display is wired or its controller is known**,
then finalize the TFT config and push the display-on firmware over OTA.

Why this exists: the first hardware flash boot-looped in `tft.begin()` (the
`spi.beginTransaction()` null-bus fault, since fixed with `USE_HSPI_PORT`).
The panel is now confirmed **ILI9486** and the display-on config is in place,
but the display's *pixel output* still can't be validated until it's physically
wired. `DISPLAY_ENABLED=0` lets a board be flashed and the rest of the system
(WiFi, control page, OTA, LEDs, I2C) verified headless before the panel goes on;
flip to `1` (the default) for the real build.

### Flashing (verified path, 2026-07-08)

Flashed from RAZORCREST (Windows bench host, Python 3.11 + esptool 5.1) with
the board on a CH343 USB-serial port:

```
python -m esptool --chip esp32s3 --port COM11 --baud 921600 \
    write_flash --flash_size 16MB 0x0 firmware.factory.bin
```

Board confirmed genuine ESP32-S3 (8 MB PSRAM, MAC ec:da:3b:9e:97:c8). The
`firmware.factory.bin` is PlatformIO's merged image (bootloader+partitions+
app) and flashes at `0x0`. **Current on-board firmware: the display-on
(`DISPLAY_ENABLED=1`) ILI9486 build** — with the `USE_HSPI_PORT` fix it boots
clean past `tft.begin()` (no more boot-loop) and the hotspot + control page +
OTA are live. NOTE: the panel is **not wired yet**, so this only proves the SPI
config no longer crashes talking to nothing — actual pixel output
(colors/orientation/inversion) is unverified until the display is connected;
adjust per the ILI9486 notes above.

## v1 firmware — `code/PipBoy3000/` (classic ESP32 fallback)

Upstream ships four near-identical sketches (Celsius/Fahrenheit ×
plain/WEBPORTAL). `code/PipBoy3000/PipBoy3000.ino` replaces all four,
keeping the WEBPORTAL feature set (Fallout boot terminal, themed WiFiManager
captive portal, graceful degradation). Configure at the top of the file:

- `#define USE_FAHRENHEIT` — comment out for Celsius.
- `#define TZ_INFO "CST6CDT,M3.2.0,M11.1.0"` — POSIX timezone (US Central
  default). DST is now handled correctly by the ESP32's own SNTP/TZ engine.
- `notification_volume` — default 25 (30 = max, distorts).

### Fixes applied (2026-07-08) vs upstream WEBPORTAL sketches

1. **Missing include** — upstream includes `images/INIT_2.h`, which doesn't
   exist in the repo; now includes `INIT.h` (`INIT3.h` is a smaller alternate
   boot GIF; both define `INIT[]`, include only one).
2. **Windows-only include paths** (`".\images/..."`) → portable `"images/..."`.
3. **Time overhaul** — `NTPClient` + hand-rolled Europe-shaped DST window +
   an unsafe `(time_t*)` cast replaced with `configTzTime()` + POSIX TZ.
   Correct US DST transitions forever, no manual `UTC` variable.
4. **Midnight bug** — 12:00–12:59 AM showed the "Afternoon" bitmap.
5. **DFPlayer event pump wired up** — `printDetail()` existed upstream but was
   never called, so SD-removed / file-error toasts never fired. Now polled in
   `loop()`.
6. **`randomSeed(esp_random())`** — radio/button sounds no longer replay the
   identical "random" sequence every boot.
7. **Toast/footer overlap** — the bottom toast bar permanently erased the
   footer bitmaps at y=300; clearing a toast now redraws the footer.
8. **Captive-portal CSS** no longer `@import`s Google Fonts (unreachable from
   inside a captive portal); falls back to Courier New.
9. Dead code removed (`conv2d`, `waitMilliseconds`, self-referential
   `#define TIME TIME` etc.), deprecated `manager.setTimeout()` →
   `setConfigPortalTimeout()`.

### Known remaining quirks in v1 (all fixed in the v2/S3 firmware)

- Button screens are blocking `while (pressed)` loops — clock/WiFi/audio
  events stall while a button is held, and each GIF restarts per pass.
- Portal AP password is literally `password`.

## Build notes

- **Board:** ESP32 Dev Module. **Partition scheme: Huge APP (3MB, no OTA)** —
  compiled firmware is 1.89 MB (verified with PlatformIO 2026-07-08: RAM 23%,
  flash 60% of Huge APP), which does not fit the default 1.3 MB app partition.
- **Libraries:** TFT_eSPI (configure `User_Setup.h` for the 4" 480×320 panel;
  enable DMA — `GIFDraw.ino` already supports it), AnimatedGIF, WiFiManager,
  DFRobotDFPlayerMini, Adafruit SHT31.
- **Pins:** buttons 25/26/27/32/33 (input pullup), DFPlayer on Serial1
  RX=16/TX=17, SHT31 on I2C 0x44.
- **DFPlayer hygiene:** 1 kΩ series resistor ESP32-TX→DFPlayer-RX (kills idle
  hiss), 100–470 µF across the DFPlayer 5 V rail (prevents brownout resets).

## SD card for the DFPlayer

FAT32, files in a folder named `MP3`, named `0001.mp3` … `0009.mp3`:
0001 = boot sound, 0002–0004 = button blips (in `sd-card/mp3/` here),
0005–0009 = radio stations (~84 MB each — NOT copied here; source them from
`.../PipBoy 3000 ESP32 - 6654866/code/git/mp3/` on the share).
