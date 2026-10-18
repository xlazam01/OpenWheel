---
name: test-fw
description: Run the FilterWheel hardware acceptance tests (pytest over USB serial) against a board on a given COM port, optionally flashing first. Use when the user says "test the firmware on COMxx" or "run acceptance". Argument: port, e.g. /test-fw COM13 [flash] [full].
---

# FilterWheel hardware tests

Suite: `tools/hwtest/` (pytest + pyserial; `pip install -r tools/hwtest/requirements.txt` if missing).
Argument: COM port (required; ask if missing). Optional words: `flash`, `full`.

## Steps (PowerShell, repo root)

1. If `flash` was given, run the `/flash-fw <PORT>` procedure first and stop if it fails.
2. Acceptance run (the gate for a "physically testable" build; moves the wheel):
   `python -m pytest tools/hwtest --port <PORT> -v`
   Prints `ACCEPTANCE PASSED/FAILED` and writes `tools/hwtest/reports/report-<time>.md`.
3. If `full` was given, ask the user to confirm that resetting calibration and reflashing is OK,
   then add `--full --confirm-destructive`. Without that confirmation use `--full` alone (only the
   100 random moves run; reset-to-defaults and the DFU round trip are skipped).
4. Report: pass/fail per test, slowest move time, the report path. On failure quote the assertion.

## Rules
- Acceptance = tests 1-5, 7-10, all must pass. Skips count as not passed.
- Setters take ~0.4 s (EEPROM write). Never send setters back to back without waiting for an ack
  (`Wheel.set` does it); do not probe the device with ad hoc setters, they change real calibration.
- The suite restores calibration after itself. If a run is aborted, read the snapshot values
  (cmds 0, 8, 10, 12, 14, 28) and compare them to the previous report or the user's notes.
- Do not use `tools/gui/filterwheel_comm.py` for tests: its command numbers for offsets
  (`:12`/`:13`) differ from the firmware (`:10`/`:11` are optical offsets, `:12`/`:13` are steps).
- If a step fails twice, stop and ask.
