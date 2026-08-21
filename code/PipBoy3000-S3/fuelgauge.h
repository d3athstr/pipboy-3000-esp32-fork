#pragma once
#include <Arduino.h>
#include <Wire.h>

// ============================================================
//  FuelGauge — generic MAX1704x driver
// ============================================================
//
// Replaces Adafruit_MAX1704X.  That library's begin() only accepts a
// MAX17048/49 — it requires (VERSION & 0xFFF0) == 0x0010 and returns false
// for anything else, so a MAX17043/44 module is refused outright even though
// it answers on the bus.  PartsBin [91] deliberately covers both ICs, so the
// board actually fitted to this PipBoy may be either one.
//
// The two families share the I2C address (0x36) and the register map.  The
// version gate is the ONLY real blocker -- the VCELL forms are numerically
// equivalent:
//
//   MAX17048/49  16-bit VCELL, 78.125 uV/LSB   ->  raw * 78.125e-6
//   MAX17043/44  12-bit left-aligned, 1.25 mV  ->  (raw >> 4) * 1.25e-3
//
// (raw >> 4) * 1.25e-3 == raw * 78.125e-6, so the families differ only in the
// low 4 bits of resolution, not in the answer.  We still pick per-part so the
// 17043's undefined low nibble is masked off rather than added as noise.
//
// SOC is 1/256 % on both.
//
// Confirmed on this build 2026-08-20: VERSION reads 0x0003, so the fitted
// board is a MAX17043 -- Adafruit's library would have refused it outright.

class FuelGauge {
public:
  static const uint8_t ADDR = 0x36;

  bool begin(TwoWire *w = &Wire) {
    _w = w;
    _present = probe();
    if (!_present) return false;
    _version = read16(REG_VERSION);
    _is48 = ((_version & 0xFFF0) == 0x0010);
    return true;
  }

  bool     present() const { return _present; }
  uint16_t version() const { return _version; }

  const char *icName() const {
    if (!_present) return "none";
    return _is48 ? "MAX17048/49" : "MAX17043/44";
  }

  float cellVoltage() {
    uint16_t raw = read16(REG_VCELL);
    return _is48 ? raw * 0.000078125f        // 78.125 uV/LSB, full 16-bit
                 : (raw >> 4) * 0.00125f;    // 1.25 mV/LSB, 12-bit aligned
  }

  float cellPercent() {
    return read16(REG_SOC) / 256.0f;         // 1/256 % on both families
  }

private:
  enum : uint8_t { REG_VCELL = 0x02, REG_SOC = 0x04, REG_VERSION = 0x08 };

  bool probe() {
    _w->beginTransmission(ADDR);
    return _w->endTransmission() == 0;
  }

  uint16_t read16(uint8_t reg) {
    _w->beginTransmission(ADDR);
    _w->write(reg);
    if (_w->endTransmission(false) != 0) return 0;
    if (_w->requestFrom((uint8_t)ADDR, (uint8_t)2) != 2) return 0;
    uint16_t v = (uint16_t)_w->read() << 8;
    v |= _w->read();
    return v;
  }

  TwoWire *_w      = &Wire;
  bool     _present = false;
  bool     _is48    = false;
  uint16_t _version = 0;
};

// Comma-separated list of every address that ACKs, e.g. "0x36,0x44,0x68".
// Used at boot and by /api/i2cscan so a missing device can be told apart
// from a device the driver refuses.
inline String i2cScanString(TwoWire &w = Wire) {
  String out;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) {
      if (out.length()) out += ",";
      out += "0x";
      if (a < 0x10) out += "0";
      out += String(a, HEX);
    }
  }
  return out.length() ? out : String("none");
}
