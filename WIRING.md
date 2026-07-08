# PipBoy 3000 v2 — Wiring Diagram (ESP32-S3)

Matches `code/PipBoy3000-S3/PipBoy3000-S3.ino` and the TFT_eSPI build flags in
README.md. Original project: <https://www.thingiverse.com/thing:6654866>.

**Graphical version:** [`wiring-diagram.svg`](wiring-diagram.svg) (open in any
browser) — same content as the ASCII diagrams below.

## Master diagram

```
                                 ┌────────────────────────────┐
                                 │     ESP32-S3 DevKitC       │
                                 │        (N16R8)             │
                                 │                            │
        5-pos rotary switch      │                            │      4" TFT 480x320 (ST7796, SPI)
        ┌─────────────┐          │                            │      ┌──────────────────┐
        │ pos1 STAT ──┼───────── │ GPIO4              GPIO11 ─┼───── │ MOSI (SDI)       │
        │ pos2 INV ───┼───────── │ GPIO5              GPIO12 ─┼───── │ SCK              │
        │ pos3 DATA ──┼───────── │ GPIO6              GPIO10 ─┼───── │ CS               │
        │ pos4 TIME ──┼───────── │ GPIO7              GPIO13 ─┼───── │ DC (RS)          │
        │ pos5 RADIO ─┼───────── │ GPIO15             GPIO14 ─┼───── │ RST              │
        │ common ─────┼── GND    │                            │      │ VCC ── 5V rail*  │
        └─────────────┘          │                            │      │ LED ── 3.3V*     │
                                 │                            │      │ GND ── GND       │
        I2C bus (3.3V)           │                            │      └──────────────────┘
        ┌──────────────┐         │                            │
        │ SHT31   0x44 ├──┬───── │ GPIO8 (SDA)                │      DFPlayer Mini
        │ DS3231  0x68 ├──┤  ┌── │ GPIO9 (SCL)        GPIO17 ─┼──[1k]── RX
        │ MAX17048 0x36├──┘  │   │                    GPIO16 ─┼──────── TX
        └──────┬───────┘─────┘   │                            │         VCC ── 5V rail
               │                 │                            │         │  └─[100-470uF]─ GND
          3.3V + GND             │                            │         GND ── GND
        (all three boards)       │                            │         SPK1 ──┐
                                 │                            │         SPK2 ──┤ speaker
        LED channel              │                            │                └── 8 ohm
   5V rail ──[100R]──►|── ... ──►│◄─ existing LED strings     │
   (anodes, existing resistors)  │                            │
        all LED cathodes ──┐     │                            │
                           │     │                            │
                 D ────────┘     │                            │
        ┌─────────┐              │                            │
        │ N-MOSFET│ G ──[100R]── │ GPIO21                     │
        │ AO3400 /│    └─[100k]─ GND  (gate pulldown)         │
        │ IRLZ44N │ S ── GND     │                            │
        └─────────┘              │ 5V(VIN)   3V3   GND        │
                                 └────┬───────┬─────┬─────────┘
                                      │       │     │
                                   5V rail  3.3V   GND
```

`*` TFT power: most 4" ST7796 modules have an onboard 3.3V regulator — check
yours. If the module has a `J1`/regulator jumper: VCC to the 5V rail with the
regulator enabled, otherwise VCC to 3.3V. Backlight `LED` pin per module spec
(usually 3.3V direct or via the module's transistor). Logic is always 3.3V —
fine directly off the S3. MISO is not connected (display is write-only here).

## Power tree

```
  LiPo/18650 ──── MAX17048 CELL pin (fuel gauge senses pack directly)
      │
      ├── BAT+/BAT- ── TP4056 charge board (USB-C in for charging)
      │                    │
      │                 OUT+/OUT-
      │                    │
      │              [power switch]
      │                    │
      └─(GND common)── 5V boost converter ──► 5V rail
                                               ├── ESP32-S3 5V/VIN pin
                                               ├── DFPlayer VCC (+bulk cap)
                                               ├── TFT VCC (see * above)
                                               └── LED anode strings (via
                                                   existing 100R resistors)

  ESP32-S3 3V3 out ──► 3.3V rail ── SHT31, DS3231, MAX17048 (VCC pins)
  All grounds common: battery, boost, ESP32, TFT, DFPlayer, sensors, MOSFET S.
```

## Pin table (ESP32-S3)

| GPIO | Direction | Connects to | Notes |
|------|-----------|-------------|-------|
| 4    | in, pullup | Rotary pos 1 (STAT) | switch common → GND |
| 5    | in, pullup | Rotary pos 2 (INV)  | |
| 6    | in, pullup | Rotary pos 3 (DATA) | |
| 7    | in, pullup | Rotary pos 4 (TIME) | |
| 15   | in, pullup | Rotary pos 5 (RADIO)| |
| 8    | I2C SDA   | SHT31 + DS3231 + MAX17048 | modules' own pullups suffice |
| 9    | I2C SCL   | SHT31 + DS3231 + MAX17048 | |
| 10   | out       | TFT CS  | |
| 11   | out       | TFT MOSI (SDI) | |
| 12   | out       | TFT SCK | |
| 13   | out       | TFT DC (RS) | |
| 14   | out       | TFT RST | |
| 16   | UART1 RX  | DFPlayer TX | direct, 3.3V-safe |
| 17   | UART1 TX  | DFPlayer RX | **through 1 kΩ series resistor** (kills hiss) |
| 21   | out, PWM  | MOSFET gate | 100 Ω series + 100 kΩ gate→GND pulldown |
| 5V/VIN | power   | 5V rail from boost | |
| 3V3  | power out | sensor boards | |
| GND  | —         | common ground | |

## Do / don't

- **Do** put the 100–470 µF electrolytic across DFPlayer VCC↔GND, close to the
  board — loud playback browns it out otherwise.
- **Do** keep the 1 kΩ in the ESP32→DFPlayer RX line; without it the speaker
  hisses constantly at idle.
- **Do** wire the MAX17048 CELL input to the battery side (before the switch),
  not the 5V rail — it measures pack voltage.
- **Don't** use GPIO 0, 3, 19/20, 45, 46 for anything (strapping/USB pins),
  and avoid 26–37 (flash/PSRAM on the N16R8 module).
- **Don't** feed the TFT or DFPlayer logic from 5V — all signals are 3.3V.
- The DS3231 needs its CR2032 installed or `rtc.lostPower()` trips every boot
  and the clock reverts to "unset" without power.
- Wago blocks from the original BOM work well as the 5V/GND distribution
  points — one block per rail.

## Bench bring-up order

1. ESP32-S3 + TFT only → flash → ROBCO boot terminal appears.
2. Add I2C boards one at a time → boot log shows RTC / fuel gauge / SHT31 OK.
3. Add DFPlayer + SD + speaker → boot sound plays, volume from control page.
4. Add MOSFET + LED strings → LIGHTS controls work from the phone.
5. Battery/charge chain last; confirm HP bar tracks a partial discharge.
```
