// PipBoy 3000 v2 — TFT_eSPI setup (ESP32-S3 + ILI9486, confirmed 2026-07-11)
//
// PlatformIO users: these are already passed via build_flags in platformio.ini
// (see README) — you do NOT need this file.
//
// Arduino IDE users: TFT_eSPI is configured through the LIBRARY, not the
// sketch. Two options:
//   A) paste the block below into TFT_eSPI/User_Setup.h, OR
//   B) copy this file into TFT_eSPI/User_Setups/ and add
//      `#include <User_Setups/TFT_eSPI_Setup_PipBoy.h>` to User_Setup_Select.h
//      (comment out the default Setup1 include there).

#define USER_SETUP_ID 9486

#define ILI9486_DRIVER          // 4" 320x480 panel controller
#define USE_HSPI_PORT           // REQUIRED on ESP32-S3: dedicated SPI port.
                                // Without it tft.begin() faults in
                                // spi.beginTransaction() and the board
                                // boot-loops.

#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// Pins (match WIRING.md / wiring-diagram.svg)
#define TFT_MISO -1             // display is write-only here
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  14

// Fonts used by the firmware
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT

#define SPI_FREQUENCY 20000000  // ILI9486 SPI is unreliable at 40MHz; 20 is safe.

// --- Panel-specific tweaks (uncomment only if the picture looks wrong) ---
// #define TFT_INVERSION_ON         // if the whole image is colour-inverted
// #define TFT_RGB_ORDER TFT_BGR    // if red and blue are swapped
