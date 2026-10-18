# OpenWheel ASCOM Driver — Specification (DRAFT v0.1)

Status: firmware (section 2) done and verified on hardware. `OpenWheel.Core` (transport, protocol, detection, worker, watchdog) is implemented and tested against a simulated misbehaving device (48 tests) and the real wheel. The ASCOM layer (`OpenWheel.Ascom`: LocalServer + IFilterWheelV3 + setup dialog) is built and passes 24 direct checks against the real wheel (`OpenWheel.Ascom.Check`); registered on the dev PC, and ConformU 4.5.2 reports no errors, warnings or issues (`conformance/ConformU-2026-10-18.txt`). Installer script `installer/OpenWheel.iss` (Inno Setup 6.7.3) compiles to a 2 MB Setup.exe; not yet run. Next: NINA test, run the installer on a clean state.
Items marked **[OPEN]** need a decision before implementation.

## 1. Goals

1. ASCOM `FilterWheel` driver for the OpenWheel (`:NN#` protocol, inherited from the myFilterWheel project) usable from NINA, APT, SGP, etc.
2. **Auto-detection** of the wheel over USB: no manual COM port selection for a normal user.
3. Expose richer state than plain ASCOM: moving, homing, calibrating, error code.
4. **Never hang** the client application, regardless of what the device (or a wrong device) answers.
5. Out of scope: INDI/Alpaca (later), Zagyl protocol (dropped).

## 2. Firmware changes required (prerequisite)

### 2.1 Unify serial speed
- `SERIALPORTSPEED` = **115200** in `config.h` and `config_g0b1.h` (currently 9600, GUI uses 115200).
- Zagyl protocol removed from the build (`SERIAL_PROTOCOL` option, `protocol_zagyl.*`, `#if` forks in `filterwheel.h`).
- Docs (`USB_CDC_CONFIGURATION.md`, `copilot-instructions.md`, GUI README) updated to 115200 only.
- Note: over USB CDC the baud is virtual; it only matters for UART builds (F103).

### 2.2 USB descriptor (stage 1 of detection)
- Unique **VID/PID** + product string `OpenWheel Filter Wheel` + per-device **serial number** (from the MCU UID).
- Replaces the generic ST `0483:5740` ("STM32 Virtual COM Port").
- **[OPEN]** VID/PID choice, see section 9 Q1.

### 2.3 Protocol additions (stage 2 of detection and status)
All new commands follow the existing `:NN[params]#` framing and reply `...#`.

| Cmd | Name | Reply |
|-----|------|-------|
| `:32#` | IDENTIFY | `OPENWHEEL,<model>,<fw>,<proto>,<serial>#` e.g. `OPENWHEEL,G0B1,FW1.0.1,2,3A00FF12#` |
| `:33#` | STATUS | `S,<state>,<slot>,<target>,<nslots>,<err>,<flags>#` (one atomic line) |
| `:34#` | GET/CLEAR ERROR | `E,<code>#`; `:34,0#` clears |

- `<state>`: 0=INIT 1=IDLE 2=MOVING 3=HOMING 4=CALIBRATING 5=ERROR (maps to `FilterWheelState`).
- `<slot>`: current slot, **-1 while moving**. `<target>`: target slot or -1.
- `<flags>` bitmask: bit0 calibrated, bit1 homed, bit2 button-pressed-during-boot, bit3 EEPROM-CRC-failed (reserved for more).
- `<proto>`: protocol version integer. Starts at 2 (legacy implicit = 1). Lets the driver refuse/adapt to old firmware.
- **Error codes** (`src/error_codes.h`): 0 none. Sticky device faults, shown in STATUS/`:34` until cleared with `:34,0#`: 1 homing timeout, 2 move timeout, 3 calibration failed, 4 EEPROM missing/write failed, 5 motor driver (TMC2209 silent or overtemp/short), 6 hall sensor not seen. Command rejections, only in the `ERR,<code>#` reply: 7 bad parameter, 8 busy, 9 not calibrated, 10 unknown command, 11 bad format/too long, 12 not supported at runtime.
  A move/home/calibration timeout is refined to 5 or 6 when the TMC2209 reports a fault or no hall pulse was seen.
- `:35#` starts calibration (needed for the driver's `Calibrate` action).
- Reply format: queries `<value>#`; set/action `OK#`; rejected `ERR,<code>#`. Parameters accept `:07 3#`, `:07,3#` and `:073#`.

## 3. Detection / connection flow

```
1. Enumerate USB serial devices (Windows registry/SetupAPI/WMI: VID, PID, serial, COM name).
2. Candidates = devices whose VID/PID match OpenWheel.            <- stage 1 (descriptor)
3. For each candidate, open @115200 (DTR off), send :25# then :32#.
   Accept only if reply parses as OPENWHEEL,... and proto >= required.   <- stage 2 (identify)
4. Remember the device by USB serial number (not COM number) so it survives COM renumbering.
5. Fallback (setup dialog only): manual COM port; still must pass stage 2.
```
- Several wheels attached: setup dialog lists them by serial number; the user picks one; selection stored in ASCOM Profile.
- Old firmware (generic ST descriptor) is **not** auto-detected; manual COM only. **[OPEN]** Q4.

## 4. ASCOM interface mapping

IFilterWheelV3 (Platform 7, async connect supported).

| ASCOM member | Implementation |
|---|---|
| `Connect()/Connecting/Disconnect()` | Async connect: detect, identify, read config on worker; `Connecting` true meanwhile. |
| `Connected` | Legacy sync wrapper around the above for old clients. |
| `Position` get | Cached from STATUS poll; **-1 while moving/homing/calibrating** (ASCOM spec). |
| `Position` set | Validate 0..N-1, send `:07<n>#`, return immediately; poll STATUS until idle. Out of range -> `InvalidValueException`. |
| `Names` | Stored **in the wheel's EEPROM** (`:36`/`:37`), read on connect and cached; editable in the setup dialog. Empty entries are reported as "Filter N". Firmware without name storage falls back to the ASCOM Profile on the PC. |
| `FocusOffsets` | Stored in the wheel (`:38`/`:39`), same rules as `Names`. NINA keeps its own list and can import these. |
| `Description`, `DriverInfo`, `DriverVersion`, `InterfaceVersion`, `Name` | Static; Name includes firmware version after connect. |
| `SupportedActions` / `Action(name, params)` | Extra info, see section 5. |
| `CommandBlind/Bool/String` | Raw passthrough, rate-limited. |

## 5. Extended info beyond the ASCOM standard

ASCOM FilterWheel has no property for "moving" other than `Position == -1`, and no error property. Surfacing more:

1. `Position == -1` while turning (standard; NINA shows this).
2. Errors raise ASCOM exceptions (`DriverException` with text + error code, `NotConnectedException` on unplug/timeouts) which NINA shows in its log/notifications.
3. `Action("Status","")` returns JSON: `{state, slot, target, slots, error, errorText, calibrated, homed, fw, serial}`.
   Other actions: `Home`, `Halt`, `Calibrate`, `ClearError`, `GetSettings`.
4. **Companion status window** (setup dialog "Live status" tab; optionally a tray app) showing the same live, since NINA does not render custom actions in its UI.
- Honest limit: NINA will not show "calibrating" or error text in its main UI on its own; only 1 and 2 are visible there.
- **[OPEN]** Q3: is a separate tray/status app wanted?

## 6. Robustness requirements

- Single owner of the serial port: one **worker thread** with a command queue. ASCOM calls never touch the port directly.
- Every command has a **timeout** (default 500 ms; reads of STATUS 300 ms). A call from a client thread never waits longer than its own deadline (property reads return cache).
- **Framing**: read until `#`; cap line length (64 B); discard garbage/binary; if a reply does not parse -> counted as failure, input buffer flushed (resync), command retried (max 2).
- **Circuit breaker**: 3 consecutive failures -> state `Faulted`, port closed, `Connected` becomes false, next call raises `NotConnectedException`; auto-reconnect attempts on a backoff timer, never blocking.
- **Move watchdog**: if a move does not finish within `moveTimeout` (default 30 s, derived from N slots), driver sends `:30#` (halt), raises error, sets Faulted-move state.
- USB unplug detection: write/read exceptions and `SerialPort` error events handled; no unhandled exceptions on worker thread.
- Poll rate: STATUS every 200 ms while moving, 1 s idle. Poll only while connected.
- Thread-safe cached state with a lock; property getters are O(1), non-blocking.
- All exceptions mapped to ASCOM exception types; no exception escapes the worker thread.
- Unit tests with a **fake serial device** (wrong answers, partial lines, silence, garbage, delayed replies, unplug) — driver test must pass ConformU.

## 7. Technology and layout

```
driver/ASCOM/
  SPEC.md                       this file
  OpenWheel.FilterWheel/        ASCOM driver project (C#, .NET Framework 4.8, ASCOM Platform 7 template)
    FilterWheel.cs              ASCOM interface class
    Transport/                  serial port, framing, worker, circuit breaker
    Detection/                  USB enumeration + identify
    SetupDialog.cs              device list, filter names, focus offsets, live status
  OpenWheel.FilterWheel.Tests/  fake-device tests
  installer/                    Inno Setup script
```
- **[OPEN]** Q5: toolchain (Visual Studio + ASCOM Platform 7 dev template, installed?).
- Registers as `ASCOM.OpenWheel.FilterWheel`. Installer: Inno Setup, registers COM/ASCOM profile.
- Validated with ASCOM **ConformU** and tested in NINA.

## 8. Python GUI / tests impact
- GUI `filterwheel_comm.py` and `tests_hw/test_full.py` updated for new commands/acks (and could reuse `:32#` for port auto-detection).

## 9. Decisions

- Q1 VID/PID: **pid.codes VID 0x1209**, wheel PID **0x2180** (planned, see `docs/USB_IDS.md`); test PID 0x0001 until the pid.codes PR is merged. Only the STM32G0B1 target remains (F103 removed).
- Q2 Acks: **every command replies** `OK#` or `ERR,<code>#`; GUI and hw tests updated.
- Q3 Status UI: **setup-dialog live-status tab only**, no tray app.
- Q4 Legacy firmware: **new firmware required**; no manual-COM fallback for old devices.
- Q5 Toolchain (probed on dev machine 2026-10-18): Visual Studio 2022 Community 17.0.4 and 2019, .NET Framework 4.8 targeting pack, ASCOM6 C# project templates, ASCOM Platform **6.6 SP2** (not 7), old Conform 6.5. Not found: Inno Setup, ConformU, `dotnet` on PATH.
  Platform 7.1 Update 3 is now installed (2026-10-18), so the driver targets **IFilterWheelV3** (async `Connect`, `DeviceState`). Architecture: `OpenWheel.Core` (net48, no ASCOM dependency, unit-tested) plus a thin ASCOM LocalServer layer on top. Install Inno Setup and ConformU before the installer/conformance stage. The installed myFilterWheel and Xagyl drivers can serve as behavioural references.

### Original questions (for reference)

1. **VID/PID**: Windows auto-detect needs a unique pair. Options: ST VID `0x0483` with a custom PID + product string/serial (cheap, can collide in principle, not allowed for commercial distribution), `pid.codes` shared VID `0x1209` with a registered PID (free, open-source hardware), or own VID (paid).
2. **Setter acknowledgements**: make every command reply (`OK#` / `ERR,<code>#`)? Cleaner and more robust; changes legacy behaviour (GUI/tests updated).
3. **Companion status app**: want a tray/status window, or setup-dialog "live status" tab only?
4. **Legacy firmware** (generic ST descriptor, no `:32`): support manual-COM fallback with a limited feature set, or require new firmware?
5. **Toolchain**: C#/.NET Framework 4.8 + Visual Studio + Platform 7, or something else?

## 10. Other suggestions (not yet agreed)
- Settings readable/writable over protocol (motor speed, direction, current, startup behaviour) so no reflash is needed; batch EEPROM writes (explicit `:23#` save) to avoid wear.
- Firmware version check in driver with a clear message when an update is needed; DFU update helper (`:31#` already exists).
- Calibration wizard in the setup dialog (progress via STATUS state 4).
- Per-slot centring adjustment in setup dialog (user offsets, `:12/:13`).
