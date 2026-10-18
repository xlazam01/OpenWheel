"""
FilterWheel Serial Communication Module
Handles serial communication with the filter wheel using myFilterWheel protocol
"""

import serial
import serial.tools.list_ports
from typing import Optional, List


class FilterWheelComm:
    """Handle serial communication with filter wheel"""

    def __init__(self):
        self.serial_port: Optional[serial.Serial] = None
        self.is_connected = False
        self.baud_rate = 115200

    def get_available_ports(self) -> List[str]:
        """Get list of available COM ports"""
        ports = serial.tools.list_ports.comports()
        return [port.device for port in ports]

    def connect(self, port: str) -> bool:
        """Connect to filter wheel on specified port"""
        try:
            # Open without asserting DTR to prevent STM32 USB CDC from resetting
            self.serial_port = serial.Serial()
            self.serial_port.port = port
            self.serial_port.baudrate = self.baud_rate
            self.serial_port.timeout = 1.0
            self.serial_port.write_timeout = 1.0
            self.serial_port.dtr = False
            self.serial_port.open()

            # Flush any startup noise then verify device responds
            self.serial_port.reset_input_buffer()
            self.is_connected = True
            for _ in range(3):
                if self.ping():
                    return True
            self.disconnect()
            return False
        except Exception as e:
            print(f"Connection error: {e}")
            self.is_connected = False
            return False

    def disconnect(self):
        """Disconnect from filter wheel"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        self.is_connected = False

    def send_command(self, command: str) -> Optional[str]:
        """Send command and return response"""
        if not self.is_connected or not self.serial_port:
            return None

        try:
            # Clear input buffer
            self.serial_port.reset_input_buffer()

            # Send command
            self.serial_port.write(command.encode())
            self.serial_port.flush()

            # Read response (terminated by #)
            response = ""
            while True:
                char = self.serial_port.read(1).decode('ascii', errors='ignore')
                if not char:  # Timeout
                    break
                response += char
                if char == '#':
                    break

            return response.strip()
        except Exception as e:
            print(f"Communication error: {e}")
            return None

    def send_command_no_response(self, command: str) -> bool:
        """Send command that doesn't return a response (setter commands)"""
        if not self.is_connected or not self.serial_port:
            return False

        try:
            self.serial_port.write(command.encode())
            self.serial_port.flush()
            return True
        except Exception as e:
            print(f"Communication error: {e}")
            return False

    # Protocol command methods (placeholders for now)

    def ping(self) -> bool:
        """Send ping command (:25#)"""
        response = self.send_command(":25#")
        return response == "OK#" if response else False

    def get_calibration_status(self) -> Optional[int]:
        """Infer calibration by checking num slots — no dedicated firmware command exists.
        Returns 1 if slots > 0 (calibrated), 0 otherwise."""
        num_slots = self.get_num_slots()
        if num_slots is None:
            return None
        return 1 if num_slots > 0 else 0

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
        """Move to specified slot (:07N#) - No response expected"""
        return self.send_command_no_response(f":07{slot}#")

    def go_home(self) -> bool:
        """Go to home position (:20#) - No response expected"""
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
        """Set base offset (:29 NNN#). Waits for the EEPROM write (~0.4 s) via a ping."""
        if not self.send_command_no_response(f":29 {offset}#"):
            return False
        old = self.serial_port.timeout
        self.serial_port.timeout = 3.0
        try:
            return self.ping()
        finally:
            self.serial_port.timeout = old

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
        """Stop movement immediately (:30#) - No response expected"""
        return self.send_command_no_response(":30#")

    def step_move(self, steps: int) -> bool:
        """Move by specific number of steps (:21SSSSS#) - No response expected"""
        return self.send_command_no_response(f":21{steps}#")

    def save_settings(self) -> bool:
        """Write settings to EEPROM (:23#) - No response expected"""
        return self.send_command_no_response(":23#")

    def reset_to_defaults(self) -> bool:
        """Reset calibration to defaults (:24#) - No response expected"""
        return self.send_command_no_response(":24#")

    def get_user_offset(self, slot: int) -> Optional[int]:
        """Get per-slot (optical) offset via :10# (all offsets), then index into result"""
        offsets = self.get_all_user_offsets()
        if offsets and 0 <= slot < len(offsets):
            return offsets[slot]
        return None

    def set_user_offset(self, slot: int, offset: int) -> bool:
        """Set per-slot (optical) offset (:11 slot,offset#). The firmware writes EEPROM
        (~0.4 s) before answering anything else, so wait for a ping to confirm it is done."""
        if not self.send_command_no_response(f":11 {slot},{offset}#"):
            return False
        old = self.serial_port.timeout
        self.serial_port.timeout = 3.0
        try:
            return self.ping()
        finally:
            self.serial_port.timeout = old

    def get_all_user_offsets(self) -> Optional[List[int]]:
        """Get per-slot (optical) offsets for all slots (:10#)"""
        response = self.send_command(":10#")
        if response and response.endswith('#'):
            try:
                offsets_str = response[:-1]
                return [int(x) for x in offsets_str.split(',')]
            except ValueError:
                return None
        return None

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
        """Set home on connect (:09 N#) - 0=no home, 1=home - No response expected"""
        return self.send_command_no_response(f":09 {behavior}#")

    def reboot_to_dfu(self) -> bool:
        """Reboot device into DFU bootloader mode (:31#) - No response expected"""
        return self.send_command_no_response(":31#")
