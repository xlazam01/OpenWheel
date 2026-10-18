# Handoff (state on 2026-10-18)

Where the project stands and what to do next. The next iteration starts when the pid.codes PID is
assigned.

## State

| Area | State |
|---|---|
| Firmware | STM32G0B1, protocol v2 (every command acknowledged, status, error codes, identify, filter names/offsets in EEPROM). Verified on a real board: `tools/hwtest` acceptance passes (10 passed). Currently built with the **test PID** `1209:0001`. |
| ASCOM driver | `driver/ASCOM`, ASCOM Platform 7.1, `IFilterWheelV3`, COM LocalServer. ConformU 4.5.2: no errors, warnings or issues (`driver/ASCOM/conformance/`). Core library has 54 unit tests against simulated misbehaving devices. Tested in NINA by the owner. |
| Installer | `driver/ASCOM/installer/OpenWheel.iss` compiles (Inno Setup 6.7.3) but has **not been run** yet. |
| pid.codes | PR <https://github.com/pidcodes/pidcodes.github.com/pull/1323> open (org `MartinsOpenAstroTools`, PID `0x2180` OpenWheel Filter Wheel). Not reviewed yet. |
| Repos | Public: <https://github.com/xlazam01/OpenWheel>. Private mirror: Gitea `martin/OpenWheel`. Private archive (never published): Gitea `martin/OpenWheel-private`, working copy in `_private/` (own git repo, ignored by this one). |

## Next iteration: after the PID is assigned

1. Check it is merged: PR 1323, then <https://pid.codes/1209/2180/>. If a maintainer asked for changes, edit the files in the fork `xlazam01/pidcodes.github.com`, branch `add-openwheel-filter-wheel` (sources in `docs/pidcodes/submit/`).
2. Firmware: in `fw/platformio.ini` set `USBD_PID=0x2180` (remove the test-PID comment), `pio run -d fw`, flash with `/flash-fw` (the board re-enumerates with a new USB ID and probably a new COM number).
3. Driver: `OpenWheel.Core` already accepts both `0x2180` and the test PID `0x0001` (`OpenWheelUsb` in `DeviceFinder.cs`). After every board is reflashed, consider dropping the shared test PID from the candidate list (other gadgets may use it).
4. Re-run: `python -m pytest tools/hwtest --port COMxx`, `dotnet test driver/ASCOM/OpenWheel.Core.Tests`, `conformu conformance ASCOM.OpenWheel.FilterWheel`. Save the new ConformU report.
5. Update `docs/USB_IDS.md` (table status), `README.md` status, `driver/ASCOM/SPEC.md`.
6. Run the installer once (see below), then publish a release build.
7. UpBox (`0x2181`) and AstroUniBoard (`0x2182`): submit their pid.codes pages (`docs/pidcodes/later/`) only when their repositories are public. Re-check that `0x2180`..`0x2184` are still free before submitting more; nothing is reserved until a PR merges.

## Open items

- Run the installer (`driver/ASCOM/installer/Output/OpenWheel-ASCOM-Setup-0.1.0.exe`, build with `ISCC.exe OpenWheel.iss`) and check install, uninstall and re-registration. It re-points the COM registration from `driver/ASCOM/dist/...` to `C:\Program Files (x86)\OpenWheel`.
- Real fault tests not done: unplug the hall sensor (expect error 6 after about 30 s), motor driver fault (error 5), EEPROM fault (error 4).
- Not agreed yet, ask before building: settings over the protocol (speed, direction, current), firmware-version check and DFU update helper in the driver, calibration wizard, per-slot centring offsets (the protocol only has the user offset of slot 0), INDI driver.
- Known: the firmware turns one way only, so moving backwards takes a full lap (about 4.6 s vs 2.1 s).
- Known: `fw/src/bootloader.c:35` has a compiler warning about undefined behaviour in the vector table copy (DFU jump). It works but should be reviewed.
- Known: `hw/production/` is not tracked now, but the first commit still contains it in the history.
- Known limit: bursts of more than about 64 bytes on the serial link can overrun the USB receive buffer; hosts must send one short command at a time.

## How to work on it

| Task | Command |
|---|---|
| Build firmware | `pio run -d fw -e STM32G0B1` (`pio` is `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`) |
| Flash over USB | `/flash-fw COMxx` (sends `:31#` to enter DFU; holding CCW at power-up also enters DFU) |
| Hardware tests | `/test-fw COMxx` or `python -m pytest tools/hwtest --port COMxx` |
| Build driver | `dotnet build driver/ASCOM/OpenWheel.Ascom -c Release` |
| Driver unit tests | `dotnet test driver/ASCOM/OpenWheel.Core.Tests` |
| Drive the driver class against a real wheel | `driver/ASCOM/OpenWheel.Ascom.Check/bin/Release/net48/OpenWheel.Ascom.Check.exe` |
| Register for development | run `driver/ASCOM/dist/OpenWheel.Ascom/OpenWheel.Ascom.Server.exe /regserver` (asks for administrator rights) |
| Update the registered build | copy `OpenWheel.Ascom/bin/Release/net48/*` over `driver/ASCOM/dist/OpenWheel.Ascom/` **only while no client is connected** (check `driver.log`, Task Manager) |

## Gotchas

- Open `fw/` in VS Code, not the repository root, or the PlatformIO extension recreates an empty project skeleton at the root (ignored by `.gitignore`).
- The wheel's COM number changed when the USB ID changed (COM13 became COM14); the driver does not care, it finds the wheel by USB ID and `:32#`.
- The driver log is `%LOCALAPPDATA%\OpenWheel\driver.log`; the settings are in the ASCOM Profile (`HKLM\SOFTWARE\WOW6432Node\ASCOM\FilterWheel Drivers\ASCOM.OpenWheel.FilterWheel`).
- NINA keeps its own filter list; the names stored in the wheel are what other software and the NINA import see.
- Line endings: some files are CRLF; edit with tools that preserve them.
