---
name: flash-fw
description: Build the FilterWheel firmware and flash it over USB (DFU) to a board on a given COM port. Use when the user says "build and upload/flash to COMxx". Takes the port as argument, e.g. /flash-fw COM13.
---

# Flash FilterWheel firmware via USB (no ST-Link, no BOOT0)

Argument: the COM port of the running filter wheel (e.g. `COM13`). If missing, list ports
(`Get-CimInstance Win32_SerialPort`) and ask which one.

Environment: `STM32G0B1` (USB CDC + DFU). `STM32F103CB` is ST-Link only, so it does not apply here.
PlatformIO: `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe` (`pio` is not on PATH).

## Steps (PowerShell, repo root; the PlatformIO project is in `fw/`)

1. **Build** `pio run -d fw -e STM32G0B1`. Stop on failure. Report flash/RAM usage.
2. **Remember the USB serial** of the device: it is the last part of the instance id
   (`Get-PnpDevice -PresentOnly | ? FriendlyName -match '\(<PORT>\)'` ->
   `USB\VID_1209&PID_0001\<SERIAL>`). Other boards (AstroUniBoard) use the same VID/PID, so
   every later step finds the wheel by this serial, never by VID/PID or "the first device".
3. **Identify first.** Open the port at 115200 with `DtrEnable=$false`, `RtsEnable=$false` (set
   before `Open()`; do not change DTR/RTS/baud on the open port, that locks the wheel again).
   Write `:25#`, expect `OK#`; then `:32#`, expect `OPENWHEEL,...,<SERIAL>#` with the serial from
   step 2. Anything else: stop and tell the user (wrong port or other device). Firmware 1.4+
   (protocol 3) answers `ERR,16` to every other command until this identify.
4. **Reboot into the bootloader.** In the same session write `:31#` (protocol cmd 31, not the DFU MCP
   tool's command, which this firmware ignores). Expect `OK#`. Close the port, wait ~4 s.
5. **Confirm DFU mode.** `dfu-util -l` (in `$env:USERPROFILE\.platformio\packages\tool-dfuutil\bin`)
   must list a `0483:df11` device with `serial="<SERIAL>"` (the ROM bootloader reports the same
   serial). If there is none, stop.
6. **Upload by serial** (not `pio ... -t upload`, which takes the first DFU device):
   `dfu-util -d 0483:df11 -S <SERIAL> -a 0 -s 0x08000000:leave -D fw\.pio\build\STM32G0B1\firmware.bin`
   - `Error during download get_status` after "File downloaded successfully" and "Submitting leave
     request" is expected on this chip (it resets while leaving DFU). Do not treat it as a failure.
   - A real failure is a missing "Download done" or a "No DFU capable device" message.
7. **Verify.** Wait ~3 s, find the COM port of `<SERIAL>` again (the number can change), open it,
   send `:25#` then `:32#` and check the serial; then `:18#` (version) and `:19#` (name) and report them.

## Rules
- Only flash a device that identified itself with the expected serial in step 3.
- Never use the `dfu-firmware-manager` MCP tool for this project.
- If a step fails twice, stop and ask.
