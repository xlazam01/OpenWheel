"""
FilterWheel Serial Communication Module
Handles serial communication with the filter wheel using the OpenWheel protocol
(see docs/PROTOCOL.md): every command is answered with "<value>#", "OK#" or "ERR,<code>#".

Identify first (protocol 3): after the port is opened the wheel accepts only :25# (ping) and
:32# (identify); everything else answers ERR,16 until :32# has been sent. connect() therefore
pings and identifies before anything else and refuses a port that is not an OpenWheel (other
devices share the USB test PID 1209:0001). Port settings (timeout, DTR, RTS, baud) are set
before open() and never changed afterwards: pyserial re-applies them on every assignment, which
the wheel sees as a new session and locks itself again.
"""

import time

import serial
import serial.tools.list_ports
from typing import Optional, List, Dict

READ_SLICE_S = 0.05      # fixed pyserial read timeout; longer waits loop over it


class FilterWheelComm:
    """Handle serial communication with filter wheel"""

    def __init__(self):
        self.serial_port: Optional[serial.Serial] = None
        self.is_connected = False
        self.baud_rate = 115200
        self.last_error_code = 0
        self.identity: Optional[Dict[str, str]] = None   # from :32# at connect

    def get_available_ports(self) -> List[str]:
        """Get list of available COM ports"""
        ports = serial.tools.list_ports.comports()
        return [port.device for port in ports]

    def connect(self, port: str) -> bool:
        """Connect to the filter wheel on the given port: ping, then identify. Nothing else is
        sent before the port has identified itself as an OpenWheel."""
        try:
            # All settings before open(); DTR not asserted (STM32 USB CDC could reset)
            self.serial_port = serial.Serial()
            self.serial_port.port = port
            self.serial_port.baudrate = self.baud_rate
            self.serial_port.timeout = READ_SLICE_S
            self.serial_port.write_timeout = 1.0
            self.serial_port.dtr = False
            self.serial_port.rts = False
            self.serial_port.open()

            # Flush any startup noise then verify device responds
            self.serial_port.reset_input_buffer()
            self.is_connected = True
            alive = any(self.ping() for _ in range(3))
            self.identity = self.identify() if alive else None
            if self.identity is None:
                print(f"{port}: not an OpenWheel (no answer to ping or identify)")
                self.disconnect()
                return False
            return True
        except Exception as e:
            print(f"Connection error: {e}")
            self.is_connected = False
            return False

    def disconnect(self):
        """Disconnect from filter wheel"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        self.is_connected = False

    def send_command(self, command: str, timeout: float = 1.0) -> Optional[str]:
        """Send command and return response (waits up to timeout seconds for the '#')"""
        if not self.is_connected or not self.serial_port:
            return None

        try:
            # Clear input buffer
            self.serial_port.reset_input_buffer()

            # Send command
            self.serial_port.write(command.encode())
            self.serial_port.flush()

            # Read response (terminated by #); never assign serial_port.timeout (relocks the wheel)
            response = ""
            end = time.time() + timeout
            while time.time() < end:
                char = self.serial_port.read(1).decode('ascii', errors='ignore')
                if not char:
                    continue
                response += char
                if char == '#':
                    break

            return response.strip()
        except Exception as e:
            print(f"Communication error: {e}")
            return None

    def send_command_no_response(self, command: str, timeout: float = 1.0) -> bool:
        """Send a setter/action command and return True when the wheel answered OK#.
        (Name kept for the callers; protocol v2 acknowledges every command.)
        On ERR,<code># the code is stored in self.last_error_code."""
        response = self.send_command(command, timeout)
        if response == "OK#":
            self.last_error_code = 0
            return True
        if response and response.startswith("ERR,"):
            try:
                self.last_error_code = int(response[4:].rstrip("#"))
            except ValueError:
                self.last_error_code = -1
        return False

    def identify(self) -> Optional[Dict[str, str]]:
        """:32# -> {'model','firmware','protocol','serial'} or None if not an OpenWheel."""
        response = self.send_command(":32#")
        if response and response.endswith("#"):
            parts = response[:-1].split(",")
            if len(parts) == 5 and parts[0] == "OPENWHEEL" and parts[4]:
                return dict(zip(("model", "firmware", "protocol", "serial"), parts[1:]))
        return None

    def get_status(self) -> Optional[Dict[str, int]]:
        """:33# -> {'state','slot','target','slots','error','flags'} or None."""
        response = self.send_command(":33#")
        if response and response.endswith("#"):
            parts = response[:-1].split(",")
            if len(parts) == 7 and parts[0] == "S":
                try:
                    return dict(zip(("state", "slot", "target", "slots", "error", "flags"),
                                    map(int, parts[1:])))
                except ValueError:
                    return None
        return None

    def get_error(self) -> Optional[int]:
        """:34# -> sticky error code (0 = none)"""
        response = self.send_command(":34#")
        if response and response.startswith("E,") and response.endswith("#"):
            try:
                return int(response[2:-1])
            except ValueError:
                return None
        return None

    def clear_error(self) -> bool:
        """:34,0# clear the sticky error"""
        return self.send_command_no_response(":34,0#")

    def calibrate(self) -> bool:
        """:35# start calibration"""
        return self.send_command_no_response(":35#")

    # Protocol command methods (placeholders for now)

    def ping(self) -> bool:
        """Send ping command (:25#)"""
        response = self.send_command(":25#")
        return response == "OK#" if response else False

    def get_calibration_status(self) -> Optional[int]:
        """1 if the wheel reports valid calibration (status flag bit0), else 0."""
        status = self.get_status()
        if status is None:
            return None
        return status["flags"] & 1

    def get_current_slot(self) -> Optional[int]:
        """Get current slot position (:06#)"""
        response = self.send_command(":06#")
        if response and response.endswith('#'):
            try:
                return int(response[:-1])
            except ValueError:
                return None
        return None

    def move_to_slot(self, slot: int) -> bool:
        """Move to specified slot (:07 N#). True if the move was accepted (OK#)."""
        return self.send_command_no_response(f":07 {slot}#")

    def go_home(self) -> bool:
        """Go to home position (:20#)"""
        return self.send_command_no_response(":20#")

    def get_firmware_version(self) -> Optional[str]:
        """Get firmware version (:18#)"""
        response = self.send_command(":18#")
        if response and response.endswith('#'):
            return response[:-1]
        return None

    def get_base_offset(self) -> Optional[int]:
        """Get base offset (:28#)"""
        response = self.send_command(":28#")
        if response and response.endswith('#'):
            try:
                return int(response[:-1])
            except ValueError:
                return None
        return None

    def set_base_offset(self, offset: int) -> bool:
        """Set base offset (:29 NNN#). The wheel answers OK# after the EEPROM write (~0.4 s)."""
        return self.send_command_no_response(f":29 {offset}#", timeout=3.0)

    def get_num_slots(self) -> Optional[int]:
        """Get number of filter slots (:16#)"""
        response = self.send_command(":16#")
        if response and response.endswith('#'):
            try:
                return int(response[:-1])
            except ValueError:
                return None
        return None

    def get_firmware_name(self) -> Optional[str]:
        """Get firmware name (:19#)"""
        response = self.send_command(":19#")
        if response and response.endswith('#'):
            return response[:-1]
        return None

    def halt(self) -> bool:
        """Stop movement immediately (:30#)"""
        return self.send_command_no_response(":30#")

    def step_move(self, steps: int) -> bool:
        """Move by specific number of steps (:21 SSSSS#)"""
        return self.send_command_no_response(f":21 {steps}#")

    def save_settings(self) -> bool:
        """Write settings to EEPROM (:23#)"""
        return self.send_command_no_response(":23#")

    def reset_to_defaults(self) -> bool:
        """Reset calibration to defaults (:24#); the wheel recalibrates on next boot"""
        return self.send_command_no_response(":24#")

    def get_user_offset(self, slot: int) -> Optional[int]:
        """User offset of a slot. The protocol only exposes the user offset of slot 0 (:00#);
        other slots are not available (:12/:13 are motor steps, NOT offsets: never use them here)."""
        if slot != 0:
            return None
        response = self.send_command(":00#")
        if response and response.endswith('#'):
            try:
                return int(response[:-1])
            except ValueError:
                return None
        return None

    def set_user_offset(self, slot: int, offset: int) -> bool:
        """Set the user offset of slot 0 (:01 N#). Other slots are not supported by the protocol."""
        if slot != 0:
            return False
        return self.send_command_no_response(f":01 {offset}#")

    def get_startup_behavior(self) -> Optional[int]:
        """Get home on connect setting (:08#)"""
        response = self.send_command(":08#")
        if response and response.endswith('#'):
            try:
                return int(response[:-1])
            except ValueError:
                return None
        return None

    def set_startup_behavior(self, behavior: int) -> bool:
        """Set home on connect (:09 N#) - 0=no home, 1=home"""
        return self.send_command_no_response(f":09 {behavior}#")

    def reboot_to_dfu(self) -> bool:
        """Reboot device into DFU bootloader mode (:31#)"""
        return self.send_command_no_response(":31#")
