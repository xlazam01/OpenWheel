# Serial protocol (version 2)

USB CDC serial port, 115200 8N1 (the baud rate is only nominal over USB). Do not assert DTR when
opening the port on boards that reset on DTR; the firmware does not gate output on DTR.

## Framing

- A command is `:NN[params]#`: `:` starts it, `NN` is a two-digit command number, `#` ends it.
- Bytes outside `:...#` are ignored. A `:` inside a command restarts it. Commands must be at most
  46 characters between `:` and `#`; longer lines are answered `ERR,11`. Hosts should send one
  command at a time and not burst more than about 64 bytes: the USB receive buffer of the wheel can
  overrun on large bursts, losing the closing `#` (then no reply comes, so always use a timeout).
- Parameters accept `:07 3#`, `:07,3#` and `:073#`; two values are written `slot,value`.
- **Every command is answered** with one line ending in `#`:

| Answer | Meaning |
|---|---|
| `<value>#` | query result (number, text, or comma separated list) |
| `OK#` | set / action accepted |
| `ERR,<code>#` | rejected, see error codes |

Setters that write the EEPROM (`save`) take about 0.4 s to answer. A move command answers `OK#` as
soon as the move has started; poll `:33#` (or `:06#`) for completion.

## Commands

| Cmd | Name | Params | Answer |
|---|---|---|---|
| 00 / 01 | get / set index adjust (user offset of slot 0) | steps | value / OK |
| 02 / 03 | get / set motor speed | | speed / `ERR,12` (compile-time) |
| 04 / 05 | get / set reverse direction | | 0/1 / `ERR,12` (compile-time) |
| 06 | current slot | | slot, `-1` while moving or position unknown |
| 07 | move to slot | slot | OK |
| 08 / 09 | get / set startup behaviour | 0 stay, 1 home | value / OK |
| 10 / 11 | get / set optical offsets | `slot,offset` | list / OK |
| 12 / 13 | get / set motor steps per slot (slots 1..N) | `slot,steps` | list / OK |
| 14 / 15 | get / set max steps | steps | value / OK |
| 16 | number of slots | | N |
| 17 | moving? | | 1 / 0 |
| 18 / 19 | firmware version / name | | text |
| 20 | home | | OK |
| 21 | move by steps (may be negative) | steps | OK |
| 22 | reboot | | OK (then resets) |
| 23 | write settings to EEPROM | | OK |
| 24 | reset calibration to defaults (recalibrates on next boot) | | OK |
| 25 | ping | | `OK#` |
| 26 / 27 | get / set homing method | | 1 / `ERR,12` |
| 28 / 29 | get / set base offset ("home found value") | steps | value / OK |
| 30 | halt | | OK |
| 31 | reboot into DFU bootloader | | OK (then resets) |
| 32 | identify | | `OPENWHEEL,<model>,<firmware>,<protocol>,<serial>#` |
| 33 | status | | `S,<state>,<slot>,<target>,<slots>,<error>,<flags>#` |
| 34 | read / clear sticky error | none / `0` | `E,<code>#` / OK |
| 35 | start calibration | | OK |

### Identify (`:32#`)

Example: `OPENWHEEL,G0B1,FW1.0.1,2,3A00FF12ABCD#`. A host should accept a port only if this parses and
the protocol version is supported. Used for automatic detection after matching the USB VID/PID.

### Status (`:33#`)

| Field | Values |
|---|---|
| state | 0 init, 1 idle, 2 moving, 3 homing, 4 calibrating, 5 error |
| slot | current slot, `-1` while moving, homing, calibrating or initialising |
| target | target slot of a slot move, else `-1` |
| slots | number of slots found by calibration (0 = not calibrated) |
| error | sticky error code (0 none) |
| flags | bit0 calibrated, bit1 position known, bit2 button held at boot, bit3 EEPROM fault |

## Error codes

Source of truth: `fw/src/error_codes.h`.

| Code | Name | Kind |
|---|---|---|
| 0 | none | |
| 1 | home timeout | sticky fault |
| 2 | move timeout | sticky fault |
| 3 | calibration failed | sticky fault |
| 4 | EEPROM missing or write failed | sticky fault |
| 5 | motor driver (TMC2209) not answering or overtemp/short | sticky fault |
| 6 | no hall sensor pulse seen while moving | sticky fault |
| 7 | bad parameter | rejection |
| 8 | busy (moving, homing or calibrating) | rejection |
| 9 | not calibrated | rejection |
| 10 | unknown command | rejection |
| 11 | bad format / line too long | rejection |
| 12 | not changeable at runtime | rejection |

Sticky faults put the wheel into the error state for 10 s (both LEDs alternate), then back to idle;
the code stays in `:33#` / `:34#` until cleared with `:34,0#`. Rejections appear only in the reply.
