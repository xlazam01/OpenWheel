"""Acceptance set: all of these must pass before a build counts as physically testable.
Runs in file order: read-only, settings, motion, abuse, reboot."""
import re
import time

import pytest


def test_1_identity(fw):
    assert fw.raw(":25#") == "OK#"
    assert fw.get(19) == "OpenWheel"
    assert re.fullmatch(r"FW\d+\.\d+\.\d+", fw.get(18)), fw.get(18)
    assert 1 <= fw.slots <= 16


def test_2_getters(fw):
    n = fw.slots
    assert fw.get_int(0) is not None
    assert fw.get_int(2) > 0
    assert fw.get_int(4) in (0, 1)
    assert -1 <= fw.get_int(6) < n
    assert fw.get_int(8) in (0, 1)
    assert len(fw.get_list(10)) == n
    steps = fw.raw(":12#")
    assert re.fullmatch(r"(-?\d+,){%d}#" % (n - 1), steps), steps  # slots 1..N-1, trailing comma
    assert fw.get_int(14) > 0
    assert fw.get_int(17) in (0, 1)
    assert fw.get_int(26) == 1


def _changed(snap):
    c = {k: (list(v) if isinstance(v, list) else v) for k, v in snap.items()}
    c["index_adjust"] += 1
    c["startup"] = 1 - c["startup"]
    c["optical"][1] += 1
    c["steps"][0] += 1
    c["max_steps"] += 1
    c["home_found"] += 1
    return c


def test_3_settings_roundtrip_and_persistence(fw):
    orig = fw.snapshot()
    new = _changed(orig)
    try:
        fw.set(1, new["index_adjust"])
        fw.set(9, new["startup"])
        fw.set(11, f"1,{new['optical'][1]}")
        fw.set(13, f"1,{new['steps'][0]}")
        fw.set(15, new["max_steps"])
        fw.set(29, new["home_found"])
        assert fw.snapshot() == new
        fw.send(":23#")
        time.sleep(0.3)
        fw.send(":22#")  # reboot: values must survive
        time.sleep(1.0)
        fw.reconnect()
        fw.wait_idle()  # startup behaviour may home
        assert fw.snapshot() == new
    finally:
        fw.restore(orig)
    fw.send(":23#")
    time.sleep(0.3)
    assert fw.snapshot() == orig


def test_4_home(fw):
    fw.send(":20#")
    fw.wait_idle()
    assert 0 <= fw.get_int(6) < fw.slots


def test_5_move_all_slots(fw):
    n = fw.slots
    cur = fw.get_int(6)
    order = list(range(n)) + [0, n // 2, 0]  # ascending, wrap-around, longest move, back
    for target in order:
        if target == cur:
            continue
        t = fw.move(target)
        assert fw.get_int(6) == target, f"asked {target}, device reports {fw.get_int(6)}"
        fw.last_move_s = max(fw.last_move_s, t)
        cur = target


def test_7_halt(fw):
    n = fw.slots
    far = (fw.get_int(6) + n // 2) % n
    fw.send(f":07{far}#")
    time.sleep(0.5)
    if not fw.moving():
        pytest.skip("move finished before halt could be tested; move too short")
    fw.send(":30#")
    end = time.time() + 3
    while time.time() < end and fw.moving():
        time.sleep(0.1)
    assert not fw.moving(), "did not stop after halt"
    fw.send(":20#")  # recover a known position
    fw.wait_idle()


def test_8_commands_ignored_while_moving(fw):
    n = fw.slots
    cur = fw.get_int(6)
    target, other = (cur + n // 2) % n, (cur + 1) % n
    if fw.last_move_s < 1.5:
        pytest.skip("moves too short to test safely (reboot/DFU commands could land after the move)")
    t0 = time.time()
    fw.send(f":07{target}#")
    time.sleep(0.3)
    assert fw.moving()
    for cmd in (f":07{other}#", ":20#", ":22#", ":31#"):
        if time.time() - t0 > fw.last_move_s * 0.5 or not fw.moving():
            pytest.fail(f"move ended too early to send {cmd} safely")
        fw.send(cmd)
        time.sleep(0.1)
    fw.wait_idle()
    assert fw.raw(":25#") == "OK#"
    assert fw.get_int(6) == target, "a command sent during the move changed the outcome"


def test_9_malformed_input(fw):
    before = fw.snapshot()
    slot = fw.get_int(6)
    n = fw.slots
    fw.send("garbage\r\n")
    fw.send(":")
    assert fw.raw(":25#") == "OK#", "a new ':' must restart the command"
    assert fw.raw("::25#") == "OK#"
    assert fw.raw(":25:25#") == "OK#"
    assert fw.raw(":#", wait=0.5) == ""
    assert fw.raw(":9#", wait=0.5) == ""
    assert fw.raw(":99#", wait=0.5) == "", "unknown command must not answer"
    assert fw.raw(":" + "9" * 200 + "#", wait=0.5) == ""
    fw.send(f":07{n + 5}#")  # out-of-range slot
    fw.send(":07-1#")
    time.sleep(0.5)
    assert not fw.moving()
    assert fw.raw(":25#") == "OK#"
    assert fw.get_int(6) == slot
    assert fw.snapshot() == before


def test_10_reboot(fw):
    before = fw.snapshot()
    version = fw.get(18)
    fw.send(":22#")
    time.sleep(1.0)
    fw.reconnect()
    fw.wait_idle()
    assert fw.get(18) == version
    assert fw.snapshot() == before
