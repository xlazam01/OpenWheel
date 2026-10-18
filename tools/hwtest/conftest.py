"""Hardware-in-the-loop test setup for the myFilterWheel firmware (USB CDC, protocol ':NN<params>#')."""
import time
from datetime import datetime
from pathlib import Path

import pytest
import serial


def pytest_addoption(parser):
    parser.addoption("--port", required=True, help="COM port of the filter wheel, e.g. COM13")
    parser.addoption("--full", action="store_true", help="also run the optional full-run tests")
    parser.addoption("--confirm-destructive", action="store_true",
                     help="allow optional tests that reset calibration or reflash the device")
    parser.addoption("--move-timeout", type=float, default=30.0,
                     help="max seconds for one move/home to finish (default 30)")


def pytest_collection_modifyitems(config, items):
    for item in items:
        if "full" in item.keywords and not config.getoption("--full"):
            item.add_marker(pytest.mark.skip(reason="optional: run with --full"))
        elif "destructive" in item.keywords and not config.getoption("--confirm-destructive"):
            item.add_marker(pytest.mark.skip(reason="destructive: needs --full --confirm-destructive"))


class Wheel:
    """Minimal protocol client. Uses firmware command numbers directly."""

    def __init__(self, port, move_timeout):
        self.port_name = port
        self.move_timeout = move_timeout
        self.ser = None
        self.last_move_s = 0.0  # slowest move seen so far, filled in by tests

    def open(self):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.write_timeout = self.port_name, 115200, 1.0, 1.0
        s.dtr = False  # asserting DTR can reset STM32 USB CDC devices
        s.open()
        s.reset_input_buffer()
        self.ser = s

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def reconnect(self, timeout=20.0):
        """Wait for the port to come back (after reboot) and for the firmware to answer a ping."""
        self.close()
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.open()
                if self.raw(":25#") == "OK#":
                    return
            except (serial.SerialException, OSError):
                pass
            self.close()
            time.sleep(0.5)
        raise AssertionError(f"{self.port_name} did not come back within {timeout}s")

    def raw(self, text, wait=1.0):
        """Send text, return response up to and including '#', or '' on timeout."""
        self.ser.reset_input_buffer()
        self.ser.write(text.encode("latin-1"))
        self.ser.flush()
        self.ser.timeout = wait
        out = b""
        while True:
            c = self.ser.read(1)
            if not c:
                break
            out += c
            if c == b"#":
                break
        return out.decode("ascii", errors="ignore")

    def send(self, text):
        """Send text that produces no response."""
        self.ser.write(text.encode("latin-1"))
        self.ser.flush()

    def get(self, n, params=""):
        r = self.raw(f":{n:02d}{params}#")
        assert r.endswith("#"), f"cmd {n}: no/partial response {r!r}"
        return r[:-1]

    def get_int(self, n):
        return int(self.get(n))

    def get_list(self, n):
        return [int(x) for x in self.get(n).split(",") if x != ""]

    def set(self, n, params):
        self.send(f":{n:02d} {params}#")
        # a setter blocks the firmware ~0.4 s (EEPROM write); a ping answers only once it is done
        assert self.raw(":25#", wait=3.0) == "OK#", f"no answer after setter {n}"

    @property
    def slots(self):
        return self.get_int(16)

    def moving(self):
        return self.get_int(17) == 1

    def wait_idle(self, timeout=None):
        """Wait for a move/home to start and finish. Returns elapsed seconds."""
        timeout = timeout or self.move_timeout
        t0 = time.time()
        time.sleep(0.3)  # allow the move to start
        while time.time() - t0 < timeout:
            if not self.moving() and self.get_int(6) != -1:
                return time.time() - t0
            time.sleep(0.1)
        raise AssertionError(f"still moving after {timeout}s")

    def move(self, slot):
        self.send(f":07{slot}#")
        return self.wait_idle()

    def snapshot(self):
        """All persistent values the protocol can read (and, except 'slots', write)."""
        return {
            "index_adjust": self.get_int(0),
            "startup": self.get_int(8),
            "optical": self.get_list(10),
            "steps": self.get_list(12),
            "max_steps": self.get_int(14),
            "slots": self.get_int(16),
            "home_found": self.get_int(28),
        }

    def restore(self, snap):
        self.set(1, snap["index_adjust"])
        self.set(9, snap["startup"])
        for i, v in enumerate(snap["optical"]):
            self.set(11, f"{i},{v}")
        for i, v in enumerate(snap["steps"], start=1):
            self.set(13, f"{i},{v}")
        self.set(15, snap["max_steps"])
        self.set(29, snap["home_found"])


@pytest.fixture(scope="session")
def fw(request):
    w = Wheel(request.config.getoption("--port"), request.config.getoption("--move-timeout"))
    w.open()
    assert w.raw(":25#") == "OK#", "device does not answer ping - wrong port or other firmware"
    w.initial = w.snapshot()
    yield w
    try:  # leave the device the way we found it
        if w.snapshot() != w.initial:
            w.restore(w.initial)
    finally:
        w.close()


# ---- report -----------------------------------------------------------------
_results = []


def pytest_runtest_logreport(report):
    if report.when == "call" or (report.when == "setup" and report.skipped):
        note = report.longreprtext.strip().splitlines()[-1] if report.failed else ""
        _results.append((report.nodeid.split("::")[-1], report.outcome, report.duration, note))


def pytest_sessionfinish(session, exitstatus):
    if not _results:
        return
    cfg = session.config
    rows = ["| test | result | s | note |", "|---|---|---|---|"]
    rows += [f"| {n} | {o} | {d:.1f} | {note} |" for n, o, d, note in _results]
    acc = [r for r in _results if not r[0].startswith("test_full_")]
    ok = bool(acc) and all(r[1] == "passed" for r in acc)
    out = Path(__file__).parent / "reports"
    out.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    (out / f"report-{stamp}.md").write_text(
        f"# FilterWheel hardware test {stamp}\n\nport: {cfg.getoption('--port')}  full: {cfg.getoption('--full')}\n\n"
        f"**ACCEPTANCE {'PASSED' if ok else 'FAILED'}**\n\n" + "\n".join(rows) + "\n", encoding="utf-8")
    cfg._acceptance = ok


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    if hasattr(config, "_acceptance"):
        terminalreporter.write_line(f"ACCEPTANCE {'PASSED' if config._acceptance else 'FAILED'}",
                                    green=config._acceptance, red=not config._acceptance, bold=True)
