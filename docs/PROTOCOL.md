# Serial protocol (version 3)

USB CDC serial port, 115200 8N1 (the baud rate is only nominal over USB). Do not assert DTR when
opening the port on boards that reset on DTR; the firmware does not gate output on DTR.

**Identify first (protocol 3).** After power-on, and again whenever a host opens the port, the wheel
accepts only `:25#` (ping) and `:32#` (identify). Every other command answers `ERR,16` and does
nothing. A `:32#` unlocks all commands until the port is opened again or the wheel resets. See
[Identify](#identify-32) for the rule and the typical host sequence.

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
| 25 | ping (always accepted, also before identify) | | `OK#` |
| 26 / 27 | get / set homing method | | 1 / `ERR,12` |
| 28 / 29 | get / set base offset ("home found value") | steps | value / OK |
| 30 | halt | | OK |
| 31 | reboot into DFU bootloader | | OK (then resets) |
| 32 | identify (always accepted; unlocks the other commands) | | `OPENWHEEL,<model>,<firmware>,<protocol>,<serial>#` |
| 33 | status | | `S,<state>,<slot>,<target>,<slots>,<error>,<flags>#` |
| 34 | read / clear sticky error | none / `0` | `E,<code>#` / OK |
| 35 | start calibration | | OK |
| 36 | get filter names of the calibrated slots | | `name0,name1,...#` (empty name = not set) |
| 37 | set filter name | `slot,name` | OK |
| 38 | get focus offsets of the calibrated slots | | `o0,o1,...#` |
| 39 | set focus offset | `slot,offset` | OK |
| 40 | housekeeping | | `H,<mcu_temp>,<vdda>,<v5>,<v5_min>,<driver_temp>,<warnings>#` |
| 41 | StallGuard threshold and readings | | `G,<threshold>,<last>,<min>,<max>#` |
| 42 | set StallGuard threshold (stored in EEPROM) | 0..255, 0 = off | OK |
| 43 / 44 | get / set status LED brightness (stored in EEPROM) | 0..100 %, 0 = off | value / OK |
| 45 | live Hall sensor state (magnet polarity check) | | `1` switched / `0` not |

### Identify (`:32#`)

Example: `OPENWHEEL,G0B1,FW1.4.0,3,207834924B31#`. A host should accept a port only if this parses and
the protocol version is supported. Used for automatic detection after matching the USB VID/PID.
The serial is the same string as the USB serial number descriptor.

**Identify first.** OpenWheel uses the shared pid.codes test PID `1209:0001`, like its sister
device AstroUniBoard and other hobby devices, so a port found by VID/PID alone may be another
device. Since protocol 3 (firmware 1.4) the wheel therefore rejects every command except `:25#` and
`:32#` with `ERR,16` (not identified) until a host has sent `:32#`. The lock comes back:

- at power-on and after every reset (`:22#`, `:31#`, watchdog, brown-out), and
- when a host opens the port. The firmware detects this from the USB CDC class requests the host's
  driver sends on open, `SET_LINE_CODING` and `SET_CONTROL_LINE_STATE`; it does **not** gate on DTR.
  Measured on Windows 11 (usbser.sys, firmware 1.4 with request counters): between one session and
  the next (close + open) the driver sent `SET_LINE_CODING` 2 to 4 times and
  `SET_CONTROL_LINE_STATE` 2 to 5 times, with DTR = false as well as DTR = true, from .NET
  `SerialPort` and from pyserial, so a new session always starts locked.

Any later `SET_LINE_CODING` / `SET_CONTROL_LINE_STATE` locks the wheel again, so a host must set
the baud rate, DTR and RTS **before** it identifies and must not change them afterwards. Pitfall:
pyserial re-applies the whole port configuration (`SetCommState`) when `timeout`, `baudrate`,
`dtr` or `rts` is assigned on an open port, which relocks the wheel; set them before `open()` and
pass per-read timeouts another way. `SerialPort.ReadTimeout` / `WriteTimeout` in .NET do not send
a request.

Typical host sequence:

1. Pick candidate ports by VID/PID (and, when reconnecting, by the USB serial number you saw before).
2. Open at 115200 with DTR/RTS as you want them for the whole session.
3. Send `:25#` until it answers `OK#` (the wheel boots for about 1.5 s after a reset).
4. Send `:32#`; accept the port only if the reply starts with `OPENWHEEL,`, the protocol version is
   supported and (when reconnecting) the serial matches. Close it otherwise, without sending
   anything else.
5. Now any command, e.g. `:33#` status, `:36#` names.
6. After `:22#` / `:31#` or a USB drop, find the port again **by serial number** and start at 2.

Firmware before 1.4 (protocol 2) has no lock; the same host sequence works with it.

### Filter names and focus offsets (`:36`..`:39`)

Stored in the wheel's EEPROM in their own region (address 256, own magic and CRC), so they survive a
calibration reset and follow the wheel to any PC. Up to 9 slots. A name is 0 to 15 printable ASCII
characters and must not contain `,` `#` `:` or `|`; leading and trailing spaces are trimmed; an empty
name clears the entry. Focus offsets are signed 16 bit. Each set command writes only the bytes that
changed and answers after about 0.25 s. Firmware older than this feature answers `ERR,10`.

### LED brightness (`:43`, `:44`)

Perceptual scale (gamma 2) by software PWM at 200 Hz; 100 % drives the LEDs directly, 0 % keeps
them dark (dark site). Default 100 %. Stored with the filter names (record version 2; a version 1
record from older firmware is migrated without losing names, offsets or the stall threshold).
`ERR,8` while the wheel moves (the EEPROM write would disturb the hall timing). Firmware 1.2+;
older firmware answers `ERR,10`.

### Hall sensor state (`:45#`)

`1#` while a magnet switches the Hall sensor, else `0#` (the debounced state the firmware uses for
its pulse timing, `HALL_IN` PA1, active low). Answers in every state, also when the wheel is not
calibrated, so a builder can check the loose magnets before gluing them into the wheel: the A3144 is
unipolar and switches only on a **south pole** at its branded face, so a magnet face that reads `1`
is south and goes outward (toward the sensor). The ASCOM setup dialog polls it in its *Magnet
polarity* check. Firmware 1.3+; older firmware answers `ERR,10`.

### Status (`:33#`)

| Field | Values |
|---|---|
| state | 0 init, 1 idle, 2 moving, 3 homing, 4 calibrating, 5 error |
| slot | current slot, `-1` while moving, homing, calibrating or initialising |
| target | target slot of a slot move, else `-1` |
| slots | number of slots found by calibration (0 = not calibrated) |
| error | sticky error code (0 none) |
| flags | bit0 calibrated, bit1 position known, bit2 button held at boot, bit3 EEPROM fault, bit4 housekeeping warning (see `:40#`) |

### Housekeeping (`:40#`)

Example: `H,312,3297,5040,4890,0,0#`. Firmware older than this feature answers `ERR,10`.

| Field | Values |
|---|---|
| mcu_temp | MCU die temperature in 0.1 °C (internal sensor, factory calibrated); reads a few °C above ambient |
| vdda | MCU supply in mV, measured against the internal reference |
| v5 | +5V rail in mV, `-1` on boards without the sense divider (HW rev 1) |
| v5_min | lowest +5V reading since the last move started (sampled every 5 ms while the motor runs), `-1` if not available |
| driver_temp | TMC2209 temperature class: `0` below 120 °C, else the threshold passed (`120`, `143`, `150`, `157`); `-1` driver not answering. Read while idle |
| warnings | bit0 +5V below 4.75 V (now or since the last move started), bit1 TMC2209 overtemperature prewarning, bit2 TMC2209 was reset by a supply dip and re-initialised (cleared with `:34,0#`), bit3 crystal not running, clocks from the internal oscillators |

### StallGuard (`:41`, `:42`)

The TMC2209 measures the motor load in StealthChop and reports it as `SG_RESULT` (0..510, lower =
more load). When the threshold is non-zero, a reading at or below `2 × threshold` while the motor
runs faster than 500 steps/s stops the move with error 13. The first 200 ms of each move are
ignored while the driver settles. Default 0 (off); the threshold is stored with the filter names,
so it survives a calibration reset.

`:41#` returns `G,<threshold>,<last>,<min>,<max>#`: the threshold and the `SG_RESULT` readings
of the last `:21` step move (`-1` if none; readings are only taken in step moves, so hall timing
is not disturbed). To tune: run `:21,2000#` a few times, note the lowest `min`, and set the
threshold to about a third of it (`:42,<value>#`). Check that normal moves and homing still work.

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
| 13 | motor stalled (StallGuard) | sticky fault |
| 14, 15 | not used by OpenWheel (AstroUniBoard uses them; codes stay aligned across both families) | |
| 16 | not identified: send `:32#` first (protocol 3) | rejection |

Sticky faults put the wheel into the error state for 10 s (both LEDs alternate), then back to idle;
the code stays in `:33#` / `:34#` until cleared with `:34,0#`. Rejections appear only in the reply.
