# User manual

How to install, connect and use an OpenWheel filter wheel. Building one: [build guide](BUILD.md).
The serial protocol for your own software: [PROTOCOL.md](PROTOCOL.md).

## At a glance

| | |
|---|---|
| Filters | 4 × 2" (Ø50.8 mm mounted cells), slots numbered 1–4 on the wheel |
| Connection | USB-C; power and data over the one cable (5 V USB, 0.5 A polyfuse) |
| Optical path | 21 mm, M48 × 0.75 on the telescope side, 6 × M2.5 on Ø62 on both plates |
| Drivers | ASCOM (Windows: NINA, SharpCap, APT, …), INDI (Linux: KStars/Ekos, Astroberry, StellarMate) |
| Controls | 2 buttons on the motor box (**+** CW, **−** CCW), 2 status LEDs under the pads |

## Mounting

- **Camera side:** the camera plate (the plate with the motor box). Player One cameras and other
  M2.5 flange cameras screw directly into its 6 threads. The plate is 3 mm: the screws must not
  reach into the chamber.
- **Telescope side:** M48 × 0.75 thread in the front plate, or an adapter on the 6 × M2.5 threads.
- The wheel adds **21 mm** to the back focus: plan it into your sensor spacing.
- Keep the motor box away from cables that pull on it; the USB-C opening is on its slanted side.

## First connection

1. Plug in the USB-C cable. The wheel appears as a USB serial port (VID `0x1209`); no driver is
   needed for the port itself on Windows 10/11 or Linux.
2. If the wheel was never calibrated (new build, or after work on the wheel), **calibrate**:
   long press **+** (CW), or hold **+** while plugging in, or *Calibrate* in the driver's setup
   dialog. The wheel turns a few laps, measures the magnet groups and stores 4 slots.
3. Install a driver (below) and connect from your imaging software.

## ASCOM driver (Windows)

1. Install the [ASCOM Platform 7](https://ascom-standards.org/) if it is not there yet.
2. Run `OpenWheel-ASCOM-Setup-x.y.z.exe` from the release page (until the first release: build and
   register it as in the README).
3. In your imaging software choose **OpenWheel Filter Wheel**. There is no COM port to pick: the
   driver finds the wheel by its USB ID and checks it with the identify command.

The **setup dialog** (gear icon in NINA) shows the live status and health and offers:

| Item | What it does |
|---|---|
| Filter names, focus offsets | stored **in the wheel**, so they follow it to any PC and to INDI |
| Home, Halt, Clear error, Calibrate | as the buttons do |
| Stall detection | *Test move* measures the motor load and suggests a threshold; *Set* stores it (0 = off) |
| LED brightness | 0–100 %, 0 = off for dark sites |
| Magnet polarity | live Hall sensor state (used while building; firmware 1.3+) |

NINA keeps its own filter list: after renaming filters in the wheel, clear NINA's filter list once
so it imports the new names. Log file: `%LOCALAPPDATA%\OpenWheel\driver.log`.

## INDI driver (Linux)

Build and install `indi_openwheel` as described in [driver/INDI/README.md](../driver/INDI/README.md),
then add **OpenWheel** as the filter wheel in the Ekos profile. Like the ASCOM driver it finds the
wheel by itself and reads names and offsets from the wheel. Not yet tested on a real wheel.

## Buttons

| Action | Result |
|---|---|
| Click **+** / **−** | next / previous slot (the wheel only turns one way: "previous" takes almost a lap) |
| Double click | home |
| Long press **+** | calibrate |
| Hold **+** while plugging in | calibrate at power-up |
| Hold **−** while plugging in | firmware update mode (USB DFU) |

## LEDs

| LED | Meaning |
|---|---|
| LED 1 blinks | moving |
| LED 2 blinks N times on arrival | arrived at slot N |
| Both blink alternately (10 s) | a fault: read it in the setup dialog or with `:34#` |

## Errors and what to do

| Code | Meaning | What to do |
|---|---|---|
| 1, 2 | homing / move timed out | check that the wheel turns by hand; roller pressure (build step 16) |
| 3 | calibration failed | magnets: all the same way round? any missing? Recalibrate |
| 4 | EEPROM missing or write failed | board fault; names and calibration can't be stored |
| 5 | motor driver fault | unplug, check the motor wires, plug in again |
| 6 | no Hall sensor pulse seen | sensor bent, turned round or too short (build step 2), or magnets reversed (build step 3) |
| 9 | not calibrated | calibrate |
| 13 | motor stalled | something blocks the wheel, or the stall threshold is too low |

Faults (1–6, 13) stay until cleared: *Clear error* in the setup dialog, or `:34,0#`.

## Changing filters

1. Unplug the USB cable.
2. Remove the 12 front-plate screws (they stay reachable with an OAG or adapter mounted) and lift
   the front plate. Mind the 5 × 7 shim on the shaft.
3. Each filter is held by 3 small screws with an O-ring under each head: loosen them, swap the
   filter, tighten gently (the screws cut into plastic).
4. Put the shim back, the front plate on, tighten the screws crosswise.
5. No recalibration is needed as long as the wheel stayed on its shaft. Update the names and focus
   offsets in the setup dialog.

## Firmware update

Hold **−** while plugging in (or send `:31#`): the wheel starts the STM32 USB DFU bootloader.
Flash the new firmware with PlatformIO (`pio run -d fw -e STM32G0B1 -t upload`) or
STM32CubeProgrammer, then unplug and plug in again. Calibration, names and offsets stay in the EEPROM.

## Care

- Keep the wheel capped when not in use; dust on 2" filters is hard to clean through the aperture.
- Don't carry it by the motor box.
- If the wheel starts slipping (moves time out), the silicone roller may be worn: replace the tube
  and set the motor again (build step 16).
