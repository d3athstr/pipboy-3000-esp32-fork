// LovyanGFX display config for the PipBoy 3000 (ESP32-S3 + 4.0" SPI panel).
//
// Replaces TFT_eSPI, which does not drive SPI correctly on the arduino-esp32
// 3.x core / ESP32-S3 (its 2.5.43 SPI bring-up either crashes or bit-bangs the
// wrong peripheral). LovyanGFX handles the S3 SPI bus properly.
//
// Pins match WIRING.md: SCLK=12, MOSI=11, MISO=-1, DC=13, CS=10, RST=14.
// If the image is garbage, swap the panel class below (ST7796 / ILI9488 /
// ILI9486) — same wiring, just the controller init differs.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX(void) {
    { auto c = _bus.config();
      c.spi_host    = SPI2_HOST;   // FSPI on the S3 (pins route via GPIO matrix)
      c.spi_mode    = 0;
      c.freq_write  = 8000000;
      c.freq_read   = 16000000;
      c.spi_3wire   = false;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk    = 12;
      c.pin_mosi    = 11;
      c.pin_miso    = -1;
      c.pin_dc      = 13;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    { auto c = _panel.config();
      c.pin_cs        = 10;
      c.pin_rst       = 14;
      c.pin_busy      = -1;
      c.panel_width   = 320;
      c.panel_height  = 480;
      c.offset_x      = 0;
      c.offset_y      = 0;
      c.offset_rotation = 0;
      c.readable      = false;
      c.invert        = false;     // flip if colours are inverted
      c.rgb_order     = false;     // set true if red/blue swapped
      c.dlen_16bit    = false;
      c.bus_shared    = false;
      _panel.config(c);
    }
    setPanel(&_panel);
  }
};

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif
