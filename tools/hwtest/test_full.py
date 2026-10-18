"""Optional tests, only with --full. Tests 11 and 12 also need --confirm-destructive."""
import os
import random
import subprocess
import time
from pathlib import Path

import pytest

pytestmark = pytest.mark.full


def test_full_6_random_moves(fw):
    n = fw.slots
    rnd = random.Random(1234)
    cur = fw.get_int(6)
    for i in range(100):
        target = rnd.choice([s for s in range(n) if s != cur])
        fw.move(target)
        got = fw.get_int(6)
        assert got == target, f"move {i}: asked {target}, got {got}"
        cur = target


@pytest.mark.destructive
def test_full_11_reset_to_defaults(fw):
    backup = fw.snapshot()
    try:
        fw.send(":24#")  # clears calibration and homes
        time.sleep(1.0)
        fw.wait_idle(timeout=max(fw.move_timeout, 60))
        assert fw.raw(":25#") == "OK#"
        assert fw.slots >= 1
    finally:
        fw.restore(backup)
        fw.send(":23#")
        time.sleep(0.3)
    assert fw.snapshot() == backup, "calibration could not be restored after reset"


@pytest.mark.destructive
def test_full_12_dfu_roundtrip(fw):
    root = Path(__file__).resolve().parent.parent
    if not (root / ".pio/build/STM32G0B1/firmware.bin").exists():
        pytest.fail("no build: run 'pio run -e STM32G0B1' first")
    pio = Path(os.environ["USERPROFILE"]) / ".platformio/penv/Scripts/pio.exe"
    version = fw.get(18)
    fw.send(":31#")
    fw.close()
    time.sleep(4)
    r = subprocess.run([str(pio), "run", "-e", "STM32G0B1", "-t", "upload"], cwd=root,
                       capture_output=True, text=True, timeout=300)
    out = r.stdout + r.stderr
    assert "Download done" in out, out[-1500:]  # trailing 'get_status' error is expected
    time.sleep(3)
    fw.reconnect()
    assert fw.get(18) == version
