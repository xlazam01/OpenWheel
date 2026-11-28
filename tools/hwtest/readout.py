"""Read-only health readout of an OpenWheel: status, error, housekeeping, calibration - decoded.

Use it first on a new, blank or misbehaving board, where the pytest fixture cannot start. It sends only
getters (no setter, no move, no EEPROM write).

    python -I tools/hwtest/readout.py COM6
    python -I tools/hwtest/readout.py --serial 207434A64B31

The port is opened with DTR/RTS low set before opening, then :25# and :32# identify it (protocol 3)."""
import argparse
import sys
import time

import serial
from serial.tools import list_ports

STATES = ["INIT", "IDLE", "MOVING", "HOMING", "CALIBRATING", "ERROR"]
ERRORS = {  # fw/src/error_codes.h
    0: "ok", 1: "HOME_TIMEOUT", 2: "MOVE_TIMEOUT", 3: "CALIB_FAILED", 4: "EEPROM",
    5: "MOTOR_DRIVER (TMC2209 not answering / overtemp / short)", 6: "HALL_NOT_SEEN",
    7: "BAD_PARAM", 8: "BUSY", 9: "NOT_CALIBRATED", 10: "UNKNOWN_CMD", 11: "BAD_FORMAT",
    12: "NOT_SUPPORTED", 13: "MOTOR_STALL", 16: "NOT_IDENTIFIED"}
FLAGS = {1: "calibrated", 2: "homed", 4: "button held at boot", 8: "EEPROM fault", 16: "housekeeping warning"}
WARNINGS = {1: "V5_LOW (+5V < 4.75 V)", 2: "DRIVER_OTPW", 4: "DRIVER_RESET", 8: "NO_CRYSTAL"}

HINTS = [
    (lambda d: d["error"] == 5 and d["warn"] & 1,
     "driver silent and +5V low: measure VBUS vs +5V across F1 (polyfuse; 9 ohm on board V3.2 gave 3.9 V)"),
    (lambda d: d["error"] == 5 and not d["warn"] & 1,
     "driver silent with +5V fine: check the UART path (R15 1k5, TMC pin 14 PDN/UART) and VCC_IO"),
    (lambda d: d["flags"] & 8, "EEPROM fault: check the 24AA64 (I2C 0x50) and its pull-ups"),
    (lambda d: d["warn"] & 8, "crystal did not start: running from the internal oscillator"),
    (lambda d: not d["flags"] & 1 and d["error"] != 5,
     "not calibrated: a blank EEPROM calibrates on boot when the motor and Hall sensor work"),
]


def bits(value, names):
    return ", ".join(n for b, n in names.items() if value & b) or "-"


def find(usb_serial):
    for p in list_ports.comports():
        if (p.serial_number or "").upper() == usb_serial.upper():
            return p.device
    sys.exit(f"no port with USB serial {usb_serial}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?")
    ap.add_argument("--serial")
    a = ap.parse_args()
    if not (a.port or a.serial):
        ap.error("give a port or --serial")
    s = serial.Serial()
    s.port = a.port or find(a.serial)
    s.baudrate, s.timeout, s.dtr, s.rts = 115200, 1.5, False, False
    s.open()
    time.sleep(0.2)
    s.reset_input_buffer()

    def q(cmd):
        s.write(f":{cmd}#".encode())
        return s.read_until(b"#").decode(errors="replace").rstrip("#")

    if q("25") != "OK":
        sys.exit(f"{s.port}: no OpenWheel ping reply")
    ident = q("32")
    print(f"{s.port}: {ident}")
    raw = {c: q(c) for c in ("33", "34", "40", "41", "45", "16", "10", "12", "14", "43")}
    s.close()

    st = raw["33"].split(",")
    hk = raw["40"].split(",")
    try:
        d = dict(state=int(st[1]), slot=int(st[2]), slots=int(st[4]), error=int(st[5]), flags=int(st[6]),
                 temp=int(hk[1]) / 10, vdda=int(hk[2]), v5=int(hk[3]), v5min=int(hk[4]),
                 drvtemp=int(hk[5]), warn=int(hk[6]))
    except (IndexError, ValueError):
        sys.exit(f"cannot parse status {raw['33']!r} / housekeeping {raw['40']!r}")

    state = STATES[d["state"]] if d["state"] < len(STATES) else d["state"]
    print(f"state    {state}, slot {d['slot']}, {d['slots']} slots")
    print(f"error    {d['error']} {ERRORS.get(d['error'], '?')}   (:34 -> {raw['34']})")
    print(f"flags    {d['flags']}: {bits(d['flags'], FLAGS)}")
    print(f"power    +5V {d['v5']} mV (min {d['v5min']}), VDDA {d['vdda']} mV, MCU {d['temp']:.1f} C")
    drv = "unknown" if d["drvtemp"] < 0 else ("< 120 C" if d["drvtemp"] == 0 else f">= {d['drvtemp']} C")
    print(f"driver   temp class {drv}; StallGuard {raw['41']}")
    print(f"warnings {d['warn']}: {bits(d['warn'], WARNINGS)}")
    print(f"hall     {raw['45']} (1 = magnet at the sensor)")
    print(f"calib    offsets [{raw['10']}]  steps/slot [{raw['12']}]  maxSteps {raw['14']}  LED {raw['43']} %")
    for test, hint in HINTS:
        if test(d):
            print(f"hint     {hint}")


if __name__ == "__main__":
    main()
