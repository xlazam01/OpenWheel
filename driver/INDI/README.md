# OpenWheel INDI driver

`indi_openwheel` drives the OpenWheel filter wheel from KStars/Ekos and other
[INDI](https://indilib.org) clients on Linux, for example a Raspberry Pi with Astroberry or StellarMate.
It is the Linux counterpart of the ASCOM driver in `driver/ASCOM/` and behaves the same way:

- **Finds the wheel by itself.** It looks for USB VID `0x1209` with PID `0x2180` or the test PID `0x0001`,
  then confirms the device with the `:32#` identify command. A manual port is still possible, but that
  device must pass the same check.
- **Never hangs the client.** It sends one short command at a time, and every command has a timeout:
  500 ms, or 2 s for writes to the EEPROM. After three timeouts or garbled replies in a row, it
  disconnects with an alert instead of waiting.
- **Shows that the wheel is moving.** The INDI slot property stays *Busy* until the wheel has arrived;
  Ekos waits on that. INDI's equivalent of ASCOM's "position −1" is that the slot property stays *Busy*.
- **Stores filter names and focus offsets in the wheel's EEPROM** (`:36`–`:39`), so they follow the
  wheel to any PC, Windows included.
- **Shows the status and health of the wheel**, and offers Home, Halt, Calibrate and Clear error.
- **Has a move watchdog.** If a move or homing has not finished after 45 s, or a calibration after
  120 s, the driver sends `:30#` (halt).

Status: builds against libindi 2.2.5. The core tests and the end-to-end indiserver test pass against the
simulator. **Not yet tested with the real wheel.**

## Building and installing

You need libindi 2.0 or newer with its development files, CMake 3.16+ and a C++17 compiler.

- **Astroberry / StellarMate / the INDI PPA:** `sudo apt install libindi-dev cmake g++`
- **Ubuntu 24.04 without the PPA:** Ubuntu's own `libindi-dev` is 1.9.9, too old. Build libindi from
  source as in [`docker/Dockerfile`](docker/Dockerfile).

```bash
cmake -S driver/INDI -B build-indi -DCMAKE_BUILD_TYPE=Release
cmake --build build-indi -j4
sudo cmake --install build-indi
sudo udevadm control --reload-rules    # then unplug and replug the wheel
```

The install puts these files in place:

| File | Where | Purpose |
|---|---|---|
| `indi_openwheel` | `/usr/local/bin` | the driver |
| `indi_openwheel.xml` | `/usr/local/share/indi` | driver description, so KStars lists the wheel under *Filter Wheels* |
| `99-openwheel.rules` | `/lib/udev/rules.d` | `/dev/openwheel` symlink, `dialout` group, ModemManager stays away |
| `openwheel_sim` | `/usr/local/bin` | simulated wheel (see below) |

KStars reads driver descriptions from `/usr/share/indi`. With the default `/usr/local` prefix, either
configure with `-DCMAKE_INSTALL_PREFIX=/usr`, or copy the XML file there.

The udev rule matters on a fresh system. ModemManager sends AT commands to every new USB serial
port, which disturbs the wheel for several seconds after you plug it in. The user that runs INDI must
be in the `dialout` group (`sudo usermod -aG dialout $USER`, then log in again).

## Using it in Ekos

1. In the Ekos profile editor, choose **OpenWheel** as the filter wheel.
2. Leave **Connection → Find wheel** on *Auto (USB ID)* and connect. The detected port is shown, by its
   stable `/dev/serial/by-id/...` name.
   If no wheel is on USB, the port in the *Port* field is tried, but only if it answers like an OpenWheel.
   *Port above* uses only the *Port* field.
3. **Filter** tab:
   - Filter names: read from the wheel. An empty entry is shown as "Filter N". A name you change is
     written to the wheel. Names have up to 15 characters, without `,` `#` `:` `|` (forbidden characters
     become `-`).
   - *Focus offsets*: one number per slot, also stored in the wheel. Ekos keeps its own focus offsets
     in its Filter Manager and does not read these; copy them over by hand once.
4. **Main Control** tab:
   - *Wheel*: Home, Halt, Calibrate, Clear error.
   - *Status*: state, error code with text, current problem.
   - *Health* lights: calibrated, position known, fault, warning, EEPROM.
5. **General Info** tab: model, firmware, protocol version, serial number, port, and the
   housekeeping readings:
   - MCU temperature
   - MCU supply
   - +5V rail, and its minimum during the last move
   - motor driver temperature class
   - warnings, as text

Older firmware without name storage (`:36` answers `ERR,10`) still works. The names then live in INDI's
config file, and the focus offset and housekeeping properties are not shown.

## Properties

| Property | Type | Content |
|---|---|---|
| `FILTER_SLOT` | number | standard INDI slot, 1..N (wheel slot + 1); *Busy* while moving |
| `FILTER_NAME` | text | standard INDI filter names, stored in the wheel |
| `DEVICE_PORT` | text | port; filled in by auto detection |
| `OPENWHEEL_DETECT` | switch | `AUTO` (USB ID) / `MANUAL` (port above) |
| `OPENWHEEL_ACTION` | switch | `HOME`, `HALT`, `CALIBRATE`, `CLEAR_ERROR` |
| `OPENWHEEL_STATUS` | text | `STATE`, `ERROR`, `PROBLEM` |
| `OPENWHEEL_HEALTH` | light | `CALIBRATED`, `POSITION_KNOWN`, `FAULT`, `WARNING`, `EEPROM` |
| `OPENWHEEL_FOCUS_OFFSETS` | number | `OFFSET_1`..`OFFSET_N`, −32768..32767, stored in the wheel |
| `OPENWHEEL_INFO` | text | `MODEL`, `FIRMWARE`, `PROTOCOL`, `SERIAL`, `PORT` |
| `OPENWHEEL_HOUSEKEEPING` | number | `MCU_TEMP`, `VDDA`, `V5`, `V5_MIN`, `DRIVER_TEMP`, `WARNINGS` (`:40#`) |
| `OPENWHEEL_WARNINGS` | text | the warning bits in words |

Scripts can use the same names, for example
`indi_setprop "OpenWheel.OPENWHEEL_ACTION.HOME=On"`.

## Trying it without hardware: the simulator

`openwheel_sim` copies the firmware's protocol and state machine on a pseudo terminal: timed moves,
homing, calibration, names and offsets with the firmware's rules, error codes, sticky faults
with the 10 s error state, and identify first (protocol 3: `ERR,16` to everything but `:25#`/`:32#`
until `:32#`).

```bash
openwheel_sim --link /tmp/openwheel --control /tmp/openwheel.ctl &
indiserver indi_openwheel
```

In the driver, choose *Port above*, enter `/tmp/openwheel` and connect. Faults are injected by writing
a line to the control FIFO, for example `echo "failnext 6" > /tmp/openwheel.ctl`:

| Line | Effect |
|---|---|
| `silent 1` / `garbage 1` / `nohash 1` / `delay 800` | link faults: no answer, junk, missing `#`, late answer |
| `fault 6` | sticky fault now (6 = no hall pulse) |
| `failnext 13` | the next move ends with this fault (13 = stalled) |
| `stuck 1` | moves never finish (watchdog) |
| `warn 1` | housekeeping warning (bit 0 = +5 V low) |
| `slots 0` | uncalibrated wheel |
| `relock 1` | lock again, as the firmware does when the port is reopened (the driver identifies again) |
| `show` | print the simulated state |
| `unplug` | close the pty, as if the USB cable were pulled |

`openwheel_sim --help` lists the start options (`--slots`, `--ms-per-slot`, `--old-firmware`, ...).

## Tests

```bash
cmake -S driver/INDI -B build-indi && cmake --build build-indi && (cd build-indi && ctest --output-on-failure)
```

- `core` (`tests/test_core.cpp`, 31 cases) needs no libindi. It runs the session code against the
  simulator over a real pty:
  - parsing
  - USB detection on a fake sysfs tree
  - moves, rejections, halt, home, calibration
  - names and offsets: round trip, only changed entries are written, invalid ones refused before
    sending
  - faults and clear, watchdog
  - old firmware, wrong protocol version
  - identify first: nothing but `:25#`/`:32#` before identify, `ERR,16`, re-identify after a
    relock, a different device after a relock drops the connection
  - silence, garbage, lost `#`, late replies, unplug
- `indiserver` (`tests/test_indiserver.sh`, 46 checks) runs `indiserver` with the driver against the
  simulator and drives it with `indi_setprop` / `indi_getprop`, the way Ekos would.

Without a libindi 2.x on the machine, use the container (Ubuntu 24.04, libindi built from source):

```bash
docker build -t openwheel-indi-build driver/INDI/docker
docker run --rm -v "$PWD/driver/INDI:/src:ro" openwheel-indi-build sh /src/docker/build-and-test.sh
```

## Layout

| Path | Content |
|---|---|
| `src/protocol.*` | replies, identify/status/housekeeping parsers, name rules, command builders |
| `src/serial_link.*` | POSIX serial: raw 115200, stale input flushed, read until `#`, bounded wait |
| `src/detect.*` | sysfs scan for the USB VID/PID, `/dev/serial/by-id` names |
| `src/wheel.*` | session: handshake, polling, failure counter, watchdog, names/offsets/housekeeping cache |
| `src/indi_openwheel.*` | the `INDI::FilterWheel` driver on top |
| `sim/` | simulated wheel (library for tests, `openwheel_sim` program) |
| `tests/` | core tests and the indiserver test |
| `docker/` | build and test container |

Only `src/indi_openwheel.*` depends on libindi. The protocol reference is
[docs/PROTOCOL.md](../../docs/PROTOCOL.md).

## Upstream

The folder follows the layout of the [indi-3rdparty](https://github.com/indilib/indi-3rdparty) drivers:
CMake, `indi_openwheel.xml.cmake`, udev rule. The plan is to submit it there as `indi-openwheel` once
both of these are done:

- pid.codes has merged PID `0x2180`
- the driver has been tested on the real wheel in Ekos
