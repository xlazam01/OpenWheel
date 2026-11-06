"""Hardware-in-the-loop test setup for the OpenWheel firmware (USB CDC, protocol ':NN<params>#').
Every command is answered: value#, OK# or ERR,<code>#.

Identify first (protocol 3): every time the port is opened the wheel accepts only :25# and :32#
until it has been identified, so Wheel.open() always pings and identifies before anything else.
The wheel is found by its USB serial number (also after a reboot), never as "the first 1209:0001
port": other devices (e.g. AstroUniBoard) share the pid.codes test PID."""
import time
from datetime import datetime
from pathlib import Path

import pytest
import serial
from serial.tools import list_ports

USB_VID = 0x1209
USB_PIDS = (0x2180, 0x0001)   # OpenWheel (pending), shared pid.codes test PID


def find_port(usb_serial):
    """COM port of the OpenWheel with this USB serial number, or None."""
    for p in list_ports.comports():
        if p.vid == USB_VID and p.pid in USB_PIDS and (p.serial_number or "").upper() == usb_serial.upper():
            return p.device
    return None


def serial_of_port(port):
    """USB serial number of a COM port, or None."""
    for p in list_ports.comports():
        if p.device.upper() == port.upper():
            return p.serial_number
    return None


def pytest_addoption(parser):
    parser.addoption("--port", help="COM port of the filter wheel, e.g. COM13 (its USB serial is then "
                                    "used to find it again after a reboot)")
    parser.addoption("--serial", help="USB serial number of the filter wheel, e.g. 207834924B31 "
                                      "(preferred: the port is looked up by it)")
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

    READ_SLICE_S = 0.05   # fixed pyserial read timeout; longer waits loop over it

    def __init__(self, usb_serial, move_timeout):
        self.usb_serial = usb_serial
        self.port_name = None
        self.move_timeout = move_timeout
        self.ser = None
        self.identity = None
        self.last_move_s = 0.0  # slowest move seen so far, filled in by tests

    def open_raw(self):
        """Open the port of this wheel (by USB serial) WITHOUT identifying: it is then locked."""
        self.port_name = find_port(self.usb_serial)
        if not self.port_name:
            raise serial.SerialException(f"no OpenWheel with USB serial {self.usb_serial}")
        s = serial.Serial()
        # Everything is configured before open(): assigning timeout/baudrate/dtr/rts on an open port
        # re-applies the port settings, the wheel sees the CDC requests and locks itself again.
        s.port, s.baudrate, s.timeout, s.write_timeout = self.port_name, 115200, self.READ_SLICE_S, 1.0
        s.dtr = False  # asserting DTR can reset STM32 USB CDC devices
        s.rts = False
        s.open()
        s.reset_input_buffer()
        self.ser = s

    def identify(self):
        """Ping until the firmware runs, then :32#; the reply must be this wheel. Returns the fields."""
        for _ in range(10):
            if self.raw(":25#", wait=0.5) == "OK#":
                break
        else:
            raise AssertionError(f"{self.port_name}: no answer to ping")
        r = self.raw(":32#")
        parts = r.rstrip("#").split(",")
        assert r.endswith("#") and len(parts) == 5 and parts[0] == "OPENWHEEL", f"not an OpenWheel: {r!r}"
        assert parts[4].upper() == self.usb_serial.upper(), f"identify serial {parts[4]} != USB {self.usb_serial}"
        self.identity = parts
        return parts

    def open(self):
        """Open and identify (the only way any other command is accepted)."""
        self.open_raw()
        self.identify()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def reconnect(self, timeout=20.0):
        """Wait for this wheel (same USB serial) to come back after a reboot, then identify."""
        self.close()
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.open()
                return
            except (serial.SerialException, OSError, AssertionError):
                pass
            self.close()
            time.sleep(0.5)
        raise AssertionError(f"OpenWheel {self.usb_serial} did not come back within {timeout}s")

    def raw(self, text, wait=1.0, chunk=None):
        """Send text, return response up to and including '#', or '' on timeout.
        chunk: send in pieces of this many bytes (the USB CDC receive buffer of the wheel
        can overrun if more than ~100 bytes arrive in one burst)."""
        self.ser.reset_input_buffer()
        data = text.encode("latin-1")
        for i in range(0, len(data), chunk or len(data) or 1):
            self.ser.write(data[i:i + (chunk or len(data))])
            self.ser.flush()
            if chunk:
                time.sleep(0.005)
        # Never assign self.ser.timeout here (see open_raw): wait by looping over short reads.
        out = b""
        end = time.time() + wait
        while time.time() < end:
            c = self.ser.read(1)
            if not c:
                continue
            out += c
            if c == b"#":
                break
        return out.decode("ascii", errors="ignore")

    def send(self, text):
        """Send raw text without reading a reply (only for deliberately malformed input)."""
        self.ser.write(text.encode("latin-1"))
        self.ser.flush()

    def cmd(self, text, expect="OK#", wait=3.0, chunk=None):
        """Send a command and assert the exact reply (default OK#). Returns the reply."""
        r = self.raw(text, wait=wait, chunk=chunk)
        assert r == expect, f"{text!r}: expected {expect!r}, got {r!r}"
        return r

    def err(self, text, code, wait=2.0, chunk=None):
        """Send a command that must be rejected with ERR,<code>#."""
        return self.cmd(text, expect=f"ERR,{code}#", wait=wait, chunk=chunk)

    def status(self):
        """Parse :33# -> dict(state, slot, target, slots, error, flags)."""
        r = self.raw(":33#")
        parts = r.rstrip("#").split(",")
        assert len(parts) == 7 and parts[0] == "S", f"bad status {r!r}"
        keys = ("state", "slot", "target", "slots", "error", "flags")
        return dict(zip(keys, map(int, parts[1:])))

    def get(self, n, params=""):
        r = self.raw(f":{n:02d}{params}#")
        assert r.endswith("#"), f"cmd {n}: no/partial response {r!r}"
        return r[:-1]

    def get_int(self, n):
        return int(self.get(n))

    def get_list(self, n):
        return [int(x) for x in self.get(n).split(",") if x != ""]

    def set(self, n, params):
        # a setter blocks the firmware ~0.4 s (EEPROM write) and then answers OK#
        self.cmd(f":{n:02d} {params}#", wait=3.0)

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
            st = self.status()
            assert st["state"] != 5 and st["error"] == 0, f"device fault during move: {st}"
            if st["state"] == 1:
                return time.time() - t0
            time.sleep(0.1)
        raise AssertionError(f"still moving after {timeout}s")

    def move(self, slot):
        self.cmd(f":07{slot}#")
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
    usb_serial = request.config.getoption("--serial")
    port = request.config.getoption("--port")
    if not usb_serial:
        if not port:
            pytest.exit("give --serial <USB serial> (preferred) or --port <COMxx>", returncode=4)
        usb_serial = serial_of_port(port)
        if not usb_serial:
            pytest.exit(f"{port}: not a USB serial port with a serial number", returncode=4)
    elif port and find_port(usb_serial) != port:
        pytest.exit(f"{port} is not the OpenWheel with USB serial {usb_serial}", returncode=4)
    w = Wheel(usb_serial, request.config.getoption("--move-timeout"))
    w.open()   # pings and identifies; fails on any other device
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
        f"# FilterWheel hardware test {stamp}\n\nport: {cfg.getoption('--port')}  serial: {cfg.getoption('--serial')}  full: {cfg.getoption('--full')}\n\n"
        f"**ACCEPTANCE {'PASSED' if ok else 'FAILED'}**\n\n" + "\n".join(rows) + "\n", encoding="utf-8")
    cfg._acceptance = ok


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    if hasattr(config, "_acceptance"):
        terminalreporter.write_line(f"ACCEPTANCE {'PASSED' if config._acceptance else 'FAILED'}",
                                    green=config._acceptance, red=not config._acceptance, bold=True)
