# OpenWheel

Open-source motorised filter wheel for astrophotography (up to 9 filters). It appears on the PC
as a USB serial device, is detected automatically, and is meant to be driven from NINA and other
ASCOM clients.

| | |
|---|---|
| MCU | STM32G0B1 (native USB CDC), PlatformIO + Arduino framework |
| Motor | stepper on a TMC2209 (UART configured) |
| Position | hall sensor + a different-length magnet per slot, calibration stored in a 24AA64 I2C EEPROM |
| Host protocol | text commands `:NN[params]#`, 115200 baud, every command is answered |
| USB ID | VID `0x1209` ([pid.codes](https://pid.codes)), PID see [docs/USB_IDS.md](docs/USB_IDS.md) |

## Repository layout

| Folder | Content |
|---|---|
| `fw/` | firmware (PlatformIO project, target `STM32G0B1`) |
| `hw/` | KiCad schematic and PCB, production files |
| `cad/` | mechanical models (Solid Edge, STEP) |
| `driver/ASCOM/` | ASCOM driver for NINA & co. (specification; implementation in progress) |
| `tools/gui/` | small Python/Qt test GUI |
| `tools/hwtest/` | pytest acceptance tests that run against a real board over USB |
| `docs/` | protocol reference, USB IDs, pid.codes submission files |

## Build and flash

```
pio run -d fw -e STM32G0B1                 # build
pio run -d fw -e STM32G0B1 -t upload       # flash over USB (DFU)
```

A running wheel can be switched into the bootloader with the command `:31#`; otherwise hold BOOT
while resetting. Open `fw/` (not the repository root) in VS Code with the PlatformIO extension.

## Using it

- **Buttons:** single click on CW / CCW moves to the next / previous slot, double click homes,
  long press (or holding CW while powering on) starts calibration.
- **LEDs:** status LED 1 blinks while moving; LED 2 blinks the slot number on arrival; both LEDs
  alternate on an error (read the cause with `:33#` / `:34#`).
- **Serial:** see [docs/PROTOCOL.md](docs/PROTOCOL.md). Quick check: send `:25#`, the wheel answers `OK#`.
- **Test GUI:** `pip install -r tools/gui/requirements.txt && python tools/gui/main.py`
- **Hardware tests:** `python -m pytest tools/hwtest --port COM13`

## Status

Firmware protocol v2 (acknowledged commands, status and error codes) is implemented and passes
the hardware acceptance tests on a real board. The test GUI and the tests speak protocol v2. The
ASCOM driver is specified in [driver/ASCOM/SPEC.md](driver/ASCOM/SPEC.md) and not built yet.

## Acknowledgements

Many thanks to the authors of the [myFilterWheel ASCOM DIY](https://sourceforge.net/projects/myfilterwheel-ascom-diy/)
project (Robert Brown, Clive Stachon and contributors, MIT licence). The basic `:NN#` serial
protocol of OpenWheel was inspired by theirs and then extended with new features: acknowledged
replies, error codes, a status command and device identification for automatic detection.
OpenWheel is an independent project and is not affiliated with myFilterWheel.

## Licences

- Software and firmware: GPL-3.0-or-later, see [LICENSE](LICENSE).
- Hardware design files (`hw/`, `cad/`): CERN-OHL-S-2.0, see [LICENSE-HARDWARE](LICENSE-HARDWARE).
- Vendored libraries in `fw/lib/` keep their own licences (TMCStepper, SparkFun External EEPROM).
