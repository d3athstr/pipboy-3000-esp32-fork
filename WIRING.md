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
        5-pos rotary switch      │                            │      4" TFT 480x320 (ILI9486, SPI)
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

`*` TFT power: most 4" ILI9486 modules have an onboard 3.3V regulator — check
yours. If the module has a `J1`/regulator jumper: VCC to the 5V rail with the
regulator enabled, otherwise VCC to 3.3V. Backlight `LED` pin per module spec
(usually 3.3V direct or via the module's transistor). Logic is always 3.3V —
fine directly off the S3. MISO is not connected (display is write-only here).

## Power wiring — point to point (build in this order)

Terminal names vary slightly by board; match by function. **Build and
meter-test each stage before adding the next.** Two Wago blocks make life
easy: one is the **5V rail**, one is the **GND rail** — everything taps those.

### Stage 1 — battery + charge + protection

| From | To |
|------|-----|
| LiPo **+** | TP4056 **B+** |
| LiPo **−** | TP4056 **B−** |

Charge by plugging USB into the TP4056's own port — works even with the
system switched off. Use a **protected** TP4056 (the kind with OUT+/OUT−).

### Stage 2 — switch + boost to 5V

| From | To |
|------|-----|
| TP4056 **OUT+** | **power switch** lug A |
| power switch **lug B** | TPS61090 **VIN** (battery input +) |
| TP4056 **OUT−** | **GND rail** |
| TPS61090 **GND** | **GND rail** |
| TPS61090 **EN/SHDN** | tie to VIN to force-enable (skip if your TPS61090 board is on by default) |
| TPS61090 **5V OUT** | **5V rail** |

**Meter check:** switch ON → 5V rail ≈ 5.0 V; switch OFF → 0 V. Do not wire
loads until this passes.

### Stage 3 — 5V rail loads (all tap the 5V Wago block)

| To | Note |
|----|------|
| ESP32-S3 **5V / VIN** pin | |
| DFPlayer **VCC** | + 100–470 µF cap across VCC↔GND right at the DFPlayer |
| TFT **VCC** | module's onboard regulator makes its own 3.3 V |
| LED anode strings | through the existing 100 Ω resistors |

### Stage 4 — 3.3V rail loads (from the ESP32-S3 **3V3 output** pin)

| To | Note |
|----|------|
| SHT31 **VCC** | I²C 0x44 |
| DS3231 **VCC** | I²C 0x68 |
| TFT **LED**/backlight | *only if your panel's backlight pin wants 3.3 V — check the silkscreen; if it draws heavily, feed it from the module VCC instead so you don't overload the ESP32's regulator* |

### Stage 5 — ground: ONE common ground

Tie all of these to the GND rail: battery **−**, TP4056 **B−/OUT−**,
TPS61090 **GND**, ESP32 **GND**, TFT **GND**, DFPlayer **GND**, SHT31 &
DS3231 **GND**, MOSFET **source**, LED cathode return.

### Later — MAX17048 fuel gauge (phased)

The gauge must read the **raw battery cell**, so tap it **before** the switch
and boost:

| From | To |
|------|-----|
| Battery **+** side (LiPo+ / TP4056 B+) | MAX17048 cell-sense input |
| SDA / SCL | shared I²C bus, GPIO8 / GPIO9 (3.3 V) |
| GND | GND rail |

Its logic-supply pin differs by breakout (some are powered from the cell,
some have a separate 3–5 V VIN) — send me the exact board when it arrives and
I'll give you the precise pins. It draws only a few µA off the cell, so
leaving it always-connected before the switch is fine.

### Bench-power caution

While flashing over USB, let USB power the board — **don't also have the
battery/boost feeding the 5V/VIN pin at the same time** (two 5 V sources
fighting). Run on battery *or* USB, not both.

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

## Phased build (MAX17048 arrives later)

The fuel gauge isn't on hand yet — that's fine. The firmware probes for it at
boot; if it's absent, `fuelOK=false`, the boot log shows
`MAX17048 NOT FOUND (0x36) / HP bar disabled` (a warning, not a failure), the
STAT screen simply omits the HP bar, and the control page shows no battery
line. When the gauge arrives, wire it to SDA/SCL + 3V3/GND and its CELL pin to
the battery side (before the TPS61090), power-cycle, and the HP bar appears —
**no reflash**. Nothing else depends on it.

## Bench bring-up order

1. ESP32-S3 + TFT only → flash → ROBCO boot terminal appears.
2. Add I2C boards one at a time → boot log shows RTC / SHT31 OK
   (fuel gauge will read NOT FOUND until it arrives — expected).
3. Add DFPlayer + SD + speaker → boot sound plays, volume from control page.
4. Add MOSFET + LED strings → LIGHTS controls work from the phone.
5. Battery/charge chain (LiPo → TP4056 → switch → TPS61090 → 5V rail).
6. Later: add MAX17048 on I2C + CELL to battery side → HP bar tracks a
   partial discharge.
```
