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

### Stage 1 — PowerBoost 500C (charger + 5V boost, one board)

The power board is an **Adafruit PowerBoost 500C** (#1944, TPS61090-based). It
**charges the LiPo AND boosts to ~5.2 V**, so it does the job of a TP4056 *and*
a separate boost — no TP4056 needed. Top-edge silkscreen pads:
`USB  GND  BAT  EN  LB  GND  5V`.

| From | To (PowerBoost pad) |
|------|-----|
| LiPo battery | **JST** connector (or the `BAT` + `GND` breakout pads) — **watch polarity**, and keep these leads **short (< 3 in)**: long/inductive battery wires can destroy the boost |
| charge | plug micro-USB into the PowerBoost's own `USB` jack (charges even while running — acts as a UPS; charge rate 500 mA) |
| PowerBoost `5V` | **5V rail** (≈ 5.2 V) |
| PowerBoost `GND` | **GND rail** |

### Stage 2 — power switch (right on the board, via EN — the illustrated way)

The switch sits on the **`EN`** pin, NOT the main current path, so it carries no
power (just a signal — can be tiny). `EN` is internally pulled **high to BAT =
ON** by default; grounding `EN` = OFF.

- **2-pin switch (simplest):** between `EN` and `GND`. Open = ON,
  closed (EN→GND) = OFF.
- **3-pin SPDT slide (full illustration):** common → `EN`, one end → `GND`,
  other end → `BAT`. One slide = EN–GND (OFF), other = EN–BAT (ON).
  ⚠ Only wire the `BAT` end if the switch is **break-before-make**; if unsure,
  wire ONLY `EN` + `GND` and skip `BAT`.

**Meter check:** switch ON → 5V rail ≈ 5.2 V (blue onboard LED lights); OFF → 0 V.
Don't wire loads until this passes.

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

Tie all of these to the GND rail: PowerBoost `GND` pads, ESP32 **GND**,
TFT **GND**, DFPlayer **GND**, SHT31 & DS3231 **GND**, MOSFET **source**,
LED cathode return. (The battery ground returns through the PowerBoost JST.)

### MAX17048 fuel gauge — boards on hand, not yet installed

**Stock (2026-08-20): 7 boards on hand**, and they are not all the same part
— see "Which board you have" below. Wire a genuine **MAX17048**; keep the
MAX17043 modules for another project (the firmware's library rejects them).

The gauge reads the **raw battery cell**. It is a ModelGauge part — it
measures **voltage only**, no sense resistor — so pack current does not have
to flow through it, but it may.

**Both boards have a JST battery port**, and on the gauge the JST and the
`+`/`−` header pins are the same net. So there are two arrangements and they
are **electrically identical** — the gauge sees the cell voltage either way:

**A — in-line** (fewest solder joints, if the connectors mate):

```
LiPo ──JST──► MAX17048 JST      (gauge + / − header = same net as its JST)
              MAX17048 + / − ──► PowerBoost battery input
```

**B — tap** (keeps pack current out of the gauge's copper):

```
LiPo  + ──┬──► PowerBoost  BAT   (JST + pin, or the BAT pad)
          └──► MAX17048    +     (thin sense wire)

LiPo  − ──┬──► PowerBoost  GND   (JST − pin, or a GND pad)
          └──► MAX17048    −  and  GND   (common ground rail)
```

Either way the power chain is
`LiPo → PowerBoost 500C → 5 V rail → ESP32 VIN` — the gauge only ever hangs
off the **cell side**, never between the PowerBoost and the ESP32. And either
way `VCC` still comes from the ESP32's **3V3**, never from the cell.

**The 5 V side does not change when you fit the gauge.** `5Vo` keeps feeding
the ESP32 `5V`/`VIN`, DFPlayer `VCC`, TFT `VCC` and the LED anodes exactly as
it does now; the EN switch and USB charging are untouched. The only change to
the power chain is *upstream* of the PowerBoost, where the cell now reaches
its battery input through the gauge (arrangement A).

Do **not** feed the gauge's `VCC` from the 5 V rail. The module tolerates
2.5–5.0 V, but its I²C pullups would then idle at 5 V — well past GPIO8/9's
3.6 V maximum. Note also that the gauge sits on the cell side of the boost, so
it **stays powered with the EN switch off** — by design, ~3 µA in hibernate,
only worth caring about over long storage (unplug the cell).

**Connectors: the PowerBoost and gauge JSTs are the same size (JST-PH 2.0),
so those two mate.** The odd one out is the cell — `[12]` is listed as **JST
1.25 mm**. Re-terminate the cell to PH 2.0 (or fit a 1.25 mm F → PH 2.0 M
adapter) and arrangement **A** becomes two mating plugs plus one soldered
pair:

```
LiPo (PH 2.0) ──────────► MAX17048 JST
MAX17048 + / − ──pigtail─► PowerBoost JST  (or its BAT / GND pads)
```

One meter check before committing to A: **continuity between the gauge's JST
`+` pin and its header `+` pad** — they should beep. That is what makes the
header a pass-through rather than a separate sense input. If they don't beep,
build **B** instead.

**Meter polarity against the silkscreen before connecting anything**, at every
joint. The cell's keyed JST was reversed relative to the PowerBoost, and a
keyed connector is not a polarity guarantee — that mistake killed the first
cell and the PowerBoost, and the damaged PowerBoost then browned out the
display for three weeks.

**Pinout — as counted on the board in hand (2026-08-20):** eight pins,
`+  −  SDA  SCL  QST  VCC  GND  ALT`. Note there is **no `BAT` pad** and no
Adafruit-style `VIN`/`Bat`/`SDI` labelling — cell and logic supply are two
separate pairs (`+`/`−` and `VCC`/`GND`), which is what makes the 3.3 V logic
feed below straightforward.

| Gauge pin | To | Note |
|-----------|----|------|
| `+` | raw cell + — the gauge JST (A) or the PowerBoost `BAT` pad (B) | the sense node — **not** the 5 V rail |
| `−` | cell − / GND rail | normally common with `GND` on the module |
| `VCC` | ESP32 **3V3** | logic / I²C pullup rail — **not** the cell |
| `GND` | GND rail | |
| `SDA` | **GPIO8** | shared bus with SHT31 + DS3231 |
| `SCL` | **GPIO9** | |
| `QST` | **leave unconnected** | quick-start; hardware reset of the gauge |
| `ALT` | **leave unconnected** | alert output; the firmware polls instead |

The IC is powered from the cell (`CELL` is the MAX17048's only supply pin), so
`+` is both the measurement input and the chip's power. `VCC` feeds only the
logic / pullup rail — tie it to the cell instead and SDA/SCL idle at ~4.2 V,
over GPIO8/9's **3.6 V absolute maximum**. When the EN switch kills the boost,
`VCC` drops to 0 V while `+` stays live: that is the intended off state. Draw
is ~3 µA hibernate / ~23 µA active, off the cell.

Check `−` to `GND` for continuity before wiring. On most of these modules they
are the same net, in which case landing both on the ground rail is harmless
and mechanically tidier. If they *don't* beep, keep them separate: `−` follows
the cell, `GND` follows the logic rail.

**Which board you have.** PartsBin `[91]` deliberately collapses MAX17048 and
MAX17043 modules onto one component (Don's ruling 2026-08-19 — do not "fix"
it), so the stock is mixed:

| Source | Qty | Board | Verdict |
|--------|-----|-------|---------|
| AliExpress (order 1832) | 4 | "MAX17048 5580" — 8-pin clone, pinout above | **use these** |
| Amazon (order 1697) | 2 | "gernie" MAX17048/17043 IIC module | meter first, see below |
| AliExpress (order 1835) | 2 | MAX17043 module | **won't be detected** |
| adjustment | 1 | unknown | identify before wiring |

Silkscreens vary between clone batches. If a board turns up with a different
label set, the function mapping is what matters (cell pair → PowerBoost `BAT`
+ ground, `VCC` → 3V3, SDA/SCL → GPIO8/9). Some clones tie `VCC` straight to
the cell node, which puts the pullups at 4.2 V. **Meter before connecting:**
power the board, leave the ESP32 off, measure SDA→GND. Above 3.6 V, do not
land it on GPIO8/9 — that board needs its own 3.3 V feed or a level shifter.

**A real MAX17043 will never be detected, and that is not a wiring fault.**
The firmware uses `Adafruit_MAX1704X`, whose `begin()` requires
`(getICversion() & 0xFFF0) == 0x0010` — MAX17048/49 only. If the HP bar stays
dark on a 17043 board, the harness is fine; the library is refusing the chip.

**No battery = silent bus.** The gauge is cell-powered, so on USB with no LiPo
attached it will not answer at `0x36`. The 2026-07-11 bring-up log's
"MAX17048 absent" is consistent with that, not with bad wiring.

Bonus: the PowerBoost's `LB` (Low-Battery) pad already goes low under 3.2 V,
so a fuel gauge is partly redundant — but `LB` is pulled to **BAT (~4.2 V),
NOT 3.3 V-safe**, so it can't wire straight to an ESP32 GPIO. If you ever want
a low-batt input, run `LB` through a divider (e.g. 100 k/100 k) or skip it in
favour of the MAX17048.

### LED channel — AS BUILT (2N3904, 3 LEDs)

The upstream design lights ~70 LEDs through a logic-level **MOSFET** (see the
master diagram / pin table). **Don's build uses only 3 LEDs**, so a small
**2N3904 NPN transistor** is used instead of the MOSFET — well within its
200 mA limit (~60–90 mA for 3 LEDs). Low-side switch on **GPIO21** (same PWM
firmware: steady / breath / flicker + brightness):

```
  5V rail ──┬──[100Ω]──▷|── LED 1 ──┐
            ├──[100Ω]──▷|── LED 2 ──┤   3 LEDs in parallel, each its own 100Ω
            └──[100Ω]──▷|── LED 3 ──┘
                                     │  ← all 3 cathodes tied together
                                 Collector (C)
  GPIO21 ──[1kΩ]── Base (B) ────────┤ 2N3904
                     │            Emitter (E)
                  [10kΩ]             │
                     └──── GND ─────GND rail
```

- LED anodes → 5V via 100 Ω (one per LED); all cathodes → **Collector**.
- **Emitter** → GND. **Base** → GPIO21 through **1 kΩ**. **10 kΩ** Base→GND
  pulldown (off at boot).
- 2N3904 pinout (TO-92, flat face toward you, legs down): **E · B · C**
  left→right — middle pin is always Base.
- If the LED count ever grows past ~5–8 (150 mA), switch to the MOSFET.

### Current budget

The PowerBoost 500C does **500 mA continuous, ~1 A peak** (if the cell can
supply it). Display backlight + DFPlayer draw are the main loads (the 3-LED
channel is negligible). Charging keeps up only below ~300 mA draw, so charge
while the prop is off/idle, not mid-use.

### Bench-power caution

While flashing the ESP32 over its USB, don't also have the PowerBoost's `5V`
feeding the ESP32 5V/VIN at the same time — two 5 V sources fight, and the
PowerBoost warns you shouldn't back-feed its `5V` pad while it's disabled.
Easiest: put an inline JST/jumper between the PowerBoost `5V` and the 5V rail
so you can disconnect it for bench flashing; run the ESP32 on its USB *or* the
PowerBoost, not both. (Charging the LiPo via the PowerBoost's own micro-USB is
always fine and independent of this.)

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
| 21   | out, PWM  | LED driver | AS BUILT: 2N3904 **base** via 1 kΩ (+10 kΩ pulldown), 3 LEDs — see "LED channel". Upstream: MOSFET gate via 100 Ω for ~70 LEDs |
| 5V/VIN | power   | 5V rail from boost | |
| 3V3  | power out | sensor boards | |
| GND  | —         | common ground | |

## Do / don't

- **Do** put the 100–470 µF electrolytic across DFPlayer VCC↔GND, close to the
  board — loud playback browns it out otherwise. It's **polarized**: `+` lead
  (long, no stripe) → VCC (pin 1); `−` lead (stripe) → GND (pin 7). In parallel
  with the existing power — you don't reroute anything. Any voltage rating
  ≥10 V is fine (a 100 V cap works, just bigger).
- **Do** keep the 1 kΩ in the ESP32→DFPlayer RX line; without it the speaker
  hisses constantly at idle.
- **Do** wire the MAX17048 `+` input to the battery side (before the switch),
  not the 5V rail — it measures pack voltage. **Do** feed its `VCC` from the
  ESP32's **3V3**, not from the cell, or the I²C pullups sit at 4.2 V and
  exceed GPIO8/9's 3.6 V maximum.
- **Don't** use GPIO 0, 3, 19/20, 45, 46 for anything (strapping/USB pins),
  and avoid 26–37 (flash/PSRAM on the N16R8 module).
- **Don't** feed the TFT or DFPlayer logic from 5V — all signals are 3.3V.
- The DS3231 needs its CR2032 installed or `rtc.lostPower()` trips every boot
  and the clock reverts to "unset" without power.
- Wago blocks from the original BOM work well as the 5V/GND distribution
  points — one block per rail.

## Phased build (MAX17048 not yet installed)

The boards arrived (7 on hand, 2026-08-20) but none is wired in yet — that's
fine. The firmware probes for it at boot; if it's absent, `fuelOK=false`, the
boot log shows `MAX17048 NOT FOUND (0x36) / HP bar disabled` (a warning, not a
failure), the STAT screen simply omits the HP bar, and the control page shows
no battery line. To install: wire `SDA`/`SCL` to GPIO8/9, `VCC` to **3V3**,
`GND`/`−` to the rail and `+` to the battery side (before the TPS61090),
power-cycle, and the HP bar appears — **no reflash**. Nothing else depends on
it. Full pin table and the board-variant warnings are in the fuel-gauge
section above.

## Bench bring-up order

1. ESP32-S3 + TFT only → flash → ROBCO boot terminal appears.
2. Add I2C boards one at a time → boot log shows RTC / SHT31 OK
   (fuel gauge will read NOT FOUND until it arrives — expected).
3. Add DFPlayer + SD + speaker → boot sound plays, volume from control page.
4. Add MOSFET + LED strings → LIGHTS controls work from the phone.
5. Power chain (LiPo → PowerBoost 500C JST, EN switch, 5V pad → 5V rail).
6. Later: add MAX17048 on I2C (`VCC`→3V3) + `+` to battery side → HP bar
   tracks a partial discharge. Confirm the IC is a 17048, not a 17043, first.
```
