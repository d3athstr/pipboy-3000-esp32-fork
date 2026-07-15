# PipBoy 3000 — Display Bring-Up Log & Troubleshooting

## ✅ RESOLVED (2026-07-15) — the display works

Final working config (in `code/PipBoy3000-S3/`, build `v5.0-ILI9488`):
- **Panel:** Haldzemo 3.5" 480×320 SPI, controller **ILI9488** (`Panel_ILI9488`).
- **SPI:** `SPI2_HOST`, pins CS10/MOSI11/SCK12/DC13/RST14, **`freq_write = 8 MHz`**
  (higher clocks corrupt frames into diagonal lines on this hand-soldered/
  matrix-routed wiring — 8 MHz renders complete images).
- **Colours:** `rgb_order = false` **and `tft.setSwapBytes(false)`** — the GIF
  pixels were byte-scrambled (green Vault Boy showed as red/blue); flipping the
  byte order fixes it. (`rgb_order=true` and a manual R/B palette swap were both
  wrong turns — the manual swap operated on byte-reversed data and turned green
  blue.)

**What the whole saga actually was: HARDWARE.** The real blockers were (1) a
**PowerBoost 500C damaged by the earlier reverse-battery event**, outputting
**3.15 V instead of 5.2 V** → the display controller browned out (grey + `0x0`
read); and (2) a **loose SPI solder joint**. Once power and the connection were
solid, it came down to just the right driver (ILI9488) and byte order. All the
library/driver/clock/board/screen swapping was chasing a symptom whose root was
power. **Lesson: when a display reads its controller ID as `0x0` and stays grey
with everything "correct," measure the actual voltage at the panel VCC first.**

**Still open (mechanical, not firmware):** the rotary switch's **position-5
contact doesn't ground GPIO15**, so page 5 (RADIO) doesn't open — verified via
`/api/status` `knob` field reading `11111` at position 5. Reflow/reseat that one
wire. The RADIO image itself loads fine (`radio_ok` was true).

---

## Historical TL;DR (kept for the record — this was the stuck state)

- **Symptom:** display is solid uniform grey. Backlight lights (on a good panel),
  but the controller never initializes — nothing ever renders.
- **Ruled out:** power, two display libraries, three drivers, multiple SPI
  clocks, the wiring harness, **three** screens, and the ESP32 board itself
  (replaced). The symptom never changed through any of it.
- **Leading hypothesis:** the panel-side pin mapping. This 4.0" module has three
  separate SPI pin groups (LCD / touch / SD) crammed together; a wire on a
  `t_*` (touch) or `sd_*` (SD) pin instead of the LCD pin gives permanent grey,
  and would be reproduced on every rebuild. **Verify the LCD pins on the new
  screen before anything else** (see "Next steps").
- **Everything else is DONE:** power (PowerBoost 500C + Wago rails), sound
  (DFPlayer), WiFi/DMZ networking, NTP clock, LED channel, remote OTA, control
  page. Only the display is open.

## The panel

- **Model:** LCDwiki-style **4.0" TFT SPI 480×320 v2.1** (silkscreen dated
  2024-02-28). XPT2046 resistive touch + microSD onboard.
- **Pin groups (the trap):**
  - **LCD:** `cs`, `sck`, `sdi` (MOSI), `sdo` (MISO), `dc`/`rs`, `reset`, `led`, `gnd`, `vcc`
  - **Touch:** `t_cs`, `t_clk`, `t_dn`, `t_out`, `t_irq`
  - **SD:** `sd_cs`/`sd_cd`, `sd_sck`, `sd_mosi`, `sd_miso`
- **Controller:** unconfirmed. Don read **ILI9486** off the chip; the 4.0"
  480×320 v2.1 form factor is commonly **ST7796** or **ILI9488**. All three were
  tried (see below) but never against a confirmed-good SPI path, so the
  controller is still effectively unidentified. The `sdo` (MISO) read-ID test
  below would settle it.

## Wiring (ESP32-S3 → panel LCD group)

| ESP32-S3 GPIO | Panel pin | Notes |
|---|---|---|
| 10 | `cs`  | LCD chip-select — **NOT** `t_cs` / SD |
| 11 | `sdi` (MOSI) | **NOT** `sd_mosi` / touch |
| 12 | `sck` | **NOT** `t_clk` / `sd_sck` |
| 13 | `dc` / `rs` | |
| 14 | `reset` | |
| —  | `sdo` (MISO) | not connected (needed only for read-ID test) |
| 5V rail | `vcc` | via Wago; must be a solid ~5 V |
| 3.3V rail | `led` (backlight) | |
| GND rail | `gnd` | |

## Firmware state

- **Library: LovyanGFX** (migrated away from TFT_eSPI — see "Library" below).
  Config in `code/PipBoy3000-S3/lgfx_pipboy.h`.
- **Driver:** `Panel_ILI9486` (swap to `Panel_ST7796` / `Panel_ILI9488` in that
  header — one line).
- **SPI:** `SPI2_HOST`, 8 MHz (conservative; raise once working).
- **Build ID in `/api/status`:** the `"build"` field reports driver + compile
  timestamp, so you can confirm which firmware is actually live after an OTA
  (`curl http://<ip>/api/status`).
- **ESP32-S3 board REPLACED 2026-07-12.** Old MAC `ec:da:3b:9e:97:c8`, new MAC
  **`84:fc:e6:5e:67:58`** → see "Network follow-up" (reservation/NetBox/DNS were
  for the old MAC).

## What was tried, and RULED OUT

| Attempt | Result |
|---|---|
| TFT_eSPI 2.5.43, default SPI port | **crash** at `tft.begin()` (null SPI bus) |
| TFT_eSPI, `USE_HSPI_PORT` | boots, but **no output** (grey) |
| TFT_eSPI, drivers ILI9488 / ST7796 / ILI9486 | all grey (but controller was brown-out at the time — invalid) |
| TFT_eSPI, SPI 20 / 10 MHz | grey |
| **LovyanGFX**, ILI9488 / ST7796 / ILI9486 | all grey |
| LovyanGFX, SPI 27 / 8 MHz | grey |
| Power: twisted-bundle rails | VCC sagged to **4 V** → controller brown-out (real problem #1, fixed) |
| Power: **Wago rails** | VCC solid **5 V**, RESET **3.3 V** — confirmed, still grey |
| Screen #1 (original) | backlight **dead** (bench-confirmed) — killed by the reverse-battery/brownout events |
| Screen #2 | backlight lit, controller grey |
| Screen #3 (same model) | no backlight (either DOA or intermittent harness) |
| Harness bypass: **dupont jumpers** direct to board | still grey → not the harness |
| SPI remap to fresh GPIOs 38–42 (suspected damaged pins) | inconclusive (device went offline mid-rewire) |
| **New ESP32-S3 board**, pins 10–14 | **still grey** → not the board's SPI pins |

**Conclusion:** with board, screens, harness, power, library, driver, and clock
all swapped and the symptom identical, the fault is the one constant —
**the panel-side pin mapping (LCD vs touch vs SD)** or a module interface-mode
quirk shared by these same-model panels.

## Two real hardware problems that WERE found and fixed along the way

1. **Reverse-polarity LiPo** — the battery JST was wired opposite the PowerBoost
   `+/−` silkscreen (a keyed connector does NOT guarantee polarity). Reverse-
   charging killed the cell (3.2 V → 0 V) and likely stressed the first screen +
   ESP32. See the gotcha in memory `pipboy-3000-project`.
2. **Twisted-bundle power rails → VCC brown-out.** All 5 V loads twisted into one
   knot dropped the display's VCC to ~4 V under load; the module's onboard
   AMS1117-3.3 needs ≥4.5 V, so the controller browned out (grey, backlight lit).
   **Fixed with Wago terminal blocks** on the 5 V and GND rails.

## Library: why LovyanGFX, not TFT_eSPI

TFT_eSPI 2.5.43 (the final release) predates the arduino-esp32 3.x core that the
pioarduino `espressif32@55.3.39` toolchain uses. On the ESP32-S3 its SPI
bring-up either **crashes** (default port, null bus) or **bit-bangs but produces
no output** (`USE_HSPI_PORT`). LovyanGFX is built for ESP32-S3 + arduino-3.x and
drives the bus correctly (boots clean, no crash). The display layer was ported:
the `tft` object is now `LGFX` (config in `lgfx_pipboy.h`), and GIFDraw needed
only a one-line fix (`&usTemp[0][0]` for the templated `pushPixels`).
`TFT_eSPI_Setup_PipBoy.h` is now **obsolete** (kept for history).

## Next steps (when the new screen arrives)

1. **Verify the LCD pins first** (the leading hypothesis). On the new panel,
   confirm each ESP32 wire lands on the **LCD** group — `cs`/`sdi`/`sck`/`dc`/
   `reset` — and **not** a `t_*` (touch) or `sd_*` (SD) pin. This is the single
   most likely cause; it survives replacing everything else.
2. **Read the controller ID** (definitive SPI test). Wire the panel's `sdo`
   (MISO) to a spare GPIO, set `pin_miso` + `readable = true` in
   `lgfx_pipboy.h`, and read register 0x04/0xD3. A valid ID = SPI reaches the
   chip and identifies the exact controller (then it's an init detail); no ID =
   SPI isn't getting there (wrong pins / interface mode).
3. **Check the module's interface-mode jumpers** (IM0–2 solder jumpers) if the
   panel has them — a module set to parallel/other mode ignores the SPI pins.
4. Once it renders: pick the correct `Panel_*` driver; if colours are inverted
   set `invert = true`, if red/blue swapped set `rgb_order = true`; then raise
   `freq_write` (try 40 MHz) for smooth GIFs.

## Network — DONE (re-onboarded to new board MAC, 2026-07-13)

The new board (`84:fc:e6:5e:67:58`) is fully re-onboarded and back on the tidy
identity:
- EdgeRouter DMZ static-mapping `PipBoy-3000` → new MAC, IP **192.168.15.60**.
- NetBox device 1550 interface MAC updated to the new MAC.
- DNS **`pipboy.empire12.net` → 192.168.15.60** (verified reachable by name).
The old MAC `ec:da:3b:9e:97:c8` is retired.

## Other subsystems fixed since the pause

- **Radio playback:** the control-page RADIO/STOP buttons sent GET but the
  firmware handlers are POST-only → silent no-op. Fixed (buttons now POST);
  verified working. The UI-sound + radio MP3s are present on the SD card.
- **SHT31** reconnected after the board swap (temp/humidity back online).
- **DFPlayer 470 µF cap** + **3-LED / 2N3904** channel wired — see WIRING.md.

**Still open: the display** (solid grey — new screen pending; see above).
