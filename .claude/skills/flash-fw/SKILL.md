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
2. **Check the port runs this firmware.** Open the port at 115200 with `DtrEnable=$true`,
   write `:25#`, expect `OK#`. If it doesn't reply, stop and tell the user (wrong port or other firmware).
3. **Remember the USB serial** of the device so it can be re-found. Get the instance id with
   `Get-PnpDevice -PresentOnly | ? FriendlyName -match '\(<PORT>\)'`.
4. **Reboot into the bootloader.** Write `:31#` to the port (protocol cmd 31, not the DFU MCP tool's
   command, which this firmware ignores). Close the port, wait ~4 s.
5. **Confirm DFU mode.** `Get-PnpDevice -PresentOnly | ? InstanceId -match 'VID_0483&PID_DF11'`
   must show exactly one device. If there is none, stop. If there is more than one, ask the user.
6. **Upload** `pio run -d fw -e STM32G0B1 -t upload` (uses dfu-util through PlatformIO).
   - `Error during download get_status` after "File downloaded successfully" and "Submitting leave
     request" is expected on this chip (it resets while leaving DFU). Do not treat it as a failure.
   - A real failure is a missing "Download done" or a "No DFU capable device" message.
7. **Verify.** Wait ~3 s, check the original COM port is back, send `:25#`, expect `OK#`.
   Better: also send `:18#` (version) and `:19#` (name) and report them.

## Rules
- Only flash a device that answered the ping in step 2.
- Never use the `dfu-firmware-manager` MCP tool for this project.
- If a step fails twice, stop and ask.
