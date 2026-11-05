"""
FilterWheel Control - Main Window
GUI application for controlling the OpenWheel filter wheel
"""

from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QPushButton, QLabel, QComboBox, QSpinBox, QTextEdit,
    QLineEdit, QStatusBar, QMessageBox
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QFont
from filterwheel_comm import FilterWheelComm


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.comm = FilterWheelComm()
        self.setup_ui()
        self.setup_timer()

    def setup_ui(self):
        """Setup the user interface"""
        self.setWindowTitle("OpenWheel Filter Control")
        self.setMinimumSize(800, 600)

        # Central widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        # Main layout
        main_layout = QVBoxLayout(central_widget)

        # Add sections
        main_layout.addWidget(self.create_connection_group())
        main_layout.addWidget(self.create_status_group())

        # Horizontal layout for filter control and calibration
        h_layout = QHBoxLayout()
        h_layout.addWidget(self.create_filter_control_group())
        h_layout.addWidget(self.create_calibration_group())
        main_layout.addLayout(h_layout)

        main_layout.addWidget(self.create_advanced_group())
        main_layout.addWidget(self.create_log_group())

        # Status bar
        self.statusBar = QStatusBar()
        self.setStatusBar(self.statusBar)
        self.statusBar.showMessage("Disconnected")

    def create_connection_group(self) -> QGroupBox:
        """Create connection control group"""
        group = QGroupBox("Connection")
        layout = QHBoxLayout()

        # COM port selection
        layout.addWidget(QLabel("COM Port:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(100)
        layout.addWidget(self.port_combo)

        # Refresh ports button
        self.refresh_btn = QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        layout.addWidget(self.refresh_btn)

        # Connect/Disconnect button
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connection)
        layout.addWidget(self.connect_btn)

        # Connection status indicator
        self.connection_status = QLabel("●")
        self.connection_status.setStyleSheet("color: red; font-size: 20px;")
        layout.addWidget(self.connection_status)

        layout.addStretch()
        group.setLayout(layout)

        # Initial port refresh
        self.refresh_ports()

        return group

    def create_status_group(self) -> QGroupBox:
        """Create status display group"""
        group = QGroupBox("Status")
        layout = QGridLayout()

        # Current position
        layout.addWidget(QLabel("Current Slot:"), 0, 0)
        self.current_slot_label = QLabel("---")
        self.current_slot_label.setFont(QFont("Arial", 16, QFont.Bold))
        layout.addWidget(self.current_slot_label, 0, 1)

        # Calibration status
        layout.addWidget(QLabel("Calibrated:"), 0, 2)
        self.calibration_status_label = QLabel("---")
        self.calibration_status_label.setFont(QFont("Arial", 12))
        layout.addWidget(self.calibration_status_label, 0, 3)

        # Firmware version
        layout.addWidget(QLabel("Firmware:"), 1, 0)
        self.firmware_label = QLabel("---")
        layout.addWidget(self.firmware_label, 1, 1)

        # Moving status
        layout.addWidget(QLabel("State:"), 1, 2)
        self.state_label = QLabel("IDLE")
        layout.addWidget(self.state_label, 1, 3)

        group.setLayout(layout)
        return group

    def create_filter_control_group(self) -> QGroupBox:
        """Create filter position control group"""
        group = QGroupBox("Filter Control")
        layout = QVBoxLayout()

        # Slot buttons grid (0-8)
        button_layout = QGridLayout()
        self.slot_buttons = []

        for i in range(9):
            btn = QPushButton(f"Slot {i}")
            btn.setMinimumHeight(40)
            btn.clicked.connect(lambda checked, slot=i: self.move_to_slot(slot))
            btn.setVisible(False)  # Hide by default until calibration is detected
            self.slot_buttons.append(btn)
            row = i // 3
            col = i % 3
            button_layout.addWidget(btn, row, col)

        layout.addLayout(button_layout)

        # Navigation buttons
        nav_layout = QHBoxLayout()

        self.prev_btn = QPushButton("◄ Previous")
        self.prev_btn.clicked.connect(self.move_previous)
        nav_layout.addWidget(self.prev_btn)

        self.home_btn = QPushButton("⌂ Home")
        self.home_btn.clicked.connect(self.go_home)
        nav_layout.addWidget(self.home_btn)

        self.next_btn = QPushButton("Next ►")
        self.next_btn.clicked.connect(self.move_next)
        nav_layout.addWidget(self.next_btn)

        layout.addLayout(nav_layout)

        group.setLayout(layout)
        return group

    def create_calibration_group(self) -> QGroupBox:
        """Create calibration and offset control group"""
        group = QGroupBox("Calibration && Offsets")
        layout = QVBoxLayout()

        # Calibration status
        status_layout = QHBoxLayout()
        status_layout.addWidget(QLabel("Status:"))
        self.calib_status_indicator = QLabel("Not Calibrated")
        self.calib_status_indicator.setStyleSheet("color: red;")
        status_layout.addWidget(self.calib_status_indicator)
        status_layout.addStretch()
        layout.addLayout(status_layout)

        # Number of slots detected
        slots_layout = QHBoxLayout()
        slots_layout.addWidget(QLabel("Slots Detected:"))
        self.slots_detected_label = QLabel("---")
        slots_layout.addWidget(self.slots_detected_label)
        slots_layout.addStretch()
        layout.addLayout(slots_layout)

        # Base offset
        offset_layout = QHBoxLayout()
        offset_layout.addWidget(QLabel("Base Offset:"))
        self.base_offset_spin = QSpinBox()
        self.base_offset_spin.setRange(0, 1000)
        self.base_offset_spin.setValue(308)
        offset_layout.addWidget(self.base_offset_spin)

        self.set_offset_btn = QPushButton("Set")
        self.set_offset_btn.clicked.connect(self.set_base_offset)
        offset_layout.addWidget(self.set_offset_btn)

        self.get_offset_btn = QPushButton("Get")
        self.get_offset_btn.clicked.connect(self.get_base_offset)
        offset_layout.addWidget(self.get_offset_btn)

        layout.addLayout(offset_layout)

        # Per-slot user offset control
        slot_offset_layout = QHBoxLayout()
        slot_offset_layout.addWidget(QLabel("Slot:"))
        self.slot_select_spin = QSpinBox()
        self.slot_select_spin.setRange(0, 8)
        self.slot_select_spin.setValue(0)
        self.slot_select_spin.setMaximumWidth(50)
        slot_offset_layout.addWidget(self.slot_select_spin)

        slot_offset_layout.addWidget(QLabel("Offset:"))
        self.user_offset_spin = QSpinBox()
        self.user_offset_spin.setRange(-500, 500)
        self.user_offset_spin.setValue(0)
        slot_offset_layout.addWidget(self.user_offset_spin)

        self.set_user_offset_btn = QPushButton("Set")
        self.set_user_offset_btn.clicked.connect(self.set_user_offset_for_slot)
        slot_offset_layout.addWidget(self.set_user_offset_btn)

        self.get_user_offset_btn = QPushButton("Get")
        self.get_user_offset_btn.clicked.connect(self.get_user_offset_for_slot)
        slot_offset_layout.addWidget(self.get_user_offset_btn)

        layout.addLayout(slot_offset_layout)

        # Info label
        info_label = QLabel("💡 Per-slot offsets fine-tune each slot position (firmware :10/:11)")
        info_label.setStyleSheet("color: gray; font-size: 9px;")
        layout.addWidget(info_label)

        layout.addStretch()
        group.setLayout(layout)
        return group

    def create_advanced_group(self) -> QGroupBox:
        """Create advanced controls group"""
        group = QGroupBox("Advanced Controls")
        layout = QHBoxLayout()

        # Step movement
        layout.addWidget(QLabel("Step Move:"))
        self.step_spin = QSpinBox()
        self.step_spin.setRange(-10000, 10000)
        self.step_spin.setValue(10)
        layout.addWidget(self.step_spin)

        self.step_move_btn = QPushButton("Execute")
        self.step_move_btn.clicked.connect(self.execute_step_move)
        layout.addWidget(self.step_move_btn)

        layout.addStretch()

        # Custom command
        layout.addWidget(QLabel("Custom Command:"))
        self.custom_cmd_input = QLineEdit()
        self.custom_cmd_input.setPlaceholderText(":XX#")
        self.custom_cmd_input.setMaximumWidth(100)
        layout.addWidget(self.custom_cmd_input)

        self.send_cmd_btn = QPushButton("Send")
        self.send_cmd_btn.clicked.connect(self.send_custom_command)
        layout.addWidget(self.send_cmd_btn)

        layout.addStretch()

        # DFU Mode button
        self.dfu_btn = QPushButton("Enter DFU Mode")
        self.dfu_btn.setStyleSheet("background-color: #FF8C00; color: white;")
        self.dfu_btn.clicked.connect(self.enter_dfu_mode)
        layout.addWidget(self.dfu_btn)

        group.setLayout(layout)
        return group

    def create_log_group(self) -> QGroupBox:
        """Create log/console group"""
        group = QGroupBox("Communication Log")
        layout = QVBoxLayout()

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMaximumHeight(150)
        self.log_text.setStyleSheet("background-color: #1e1e1e; color: #d4d4d4; font-family: Consolas;")
        layout.addWidget(self.log_text)

        # Clear log button
        clear_layout = QHBoxLayout()
        clear_layout.addStretch()
        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(self.log_text.clear)
        clear_layout.addWidget(clear_btn)
        layout.addLayout(clear_layout)

        group.setLayout(layout)
        return group

    def setup_timer(self):
        """Setup periodic status update timer"""
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self.update_status)
        # Timer will start when connected

    # ========== Slot Implementations (Placeholders) ==========

    def refresh_ports(self):
        """Refresh available COM ports"""
        self.port_combo.clear()
        ports = self.comm.get_available_ports()
        self.port_combo.addItems(ports)
        self.log(f"Found {len(ports)} COM port(s)")

    def toggle_connection(self):
        """Connect or disconnect from filter wheel"""
        if not self.comm.is_connected:
            port = self.port_combo.currentText()
            if not port:
                QMessageBox.warning(self, "Error", "No COM port selected")
                return

            self.log(f"Connecting to {port}...")
            if self.comm.connect(port):
                self.log(f"Connected to {port}")
                self.connection_status.setStyleSheet("color: green; font-size: 20px;")
                self.connect_btn.setText("Disconnect")
                self.statusBar.showMessage(f"Connected to {port}")

                # Get firmware info on connect
                fw_version = self.comm.get_firmware_version()
                if fw_version:
                    self.firmware_label.setText(fw_version)
                    self.log(f"Firmware: {fw_version}")

                fw_name = self.comm.get_firmware_name()
                if fw_name:
                    self.log(f"Device: {fw_name}")

                # Get base offset on connect
                base_offset = self.comm.get_base_offset()
                if base_offset is not None:
                    self.base_offset_spin.setValue(base_offset)
                    self.log(f"Base offset: {base_offset}")

                self.status_timer.start(1000)  # Update every second
                self.update_status()  # Immediate update
            else:
                self.log(f"Failed to connect to {port}")
                QMessageBox.critical(self, "Connection Error", f"Could not connect to {port}")
        else:
            self.comm.disconnect()
            self.log("Disconnected")
            self.connection_status.setStyleSheet("color: red; font-size: 20px;")
            self.connect_btn.setText("Connect")
            self.statusBar.showMessage("Disconnected")
            self.status_timer.stop()

    def update_status(self):
        """Update status information from filter wheel"""
        if not self.comm.is_connected:
            return

        # Update current slot (check if moving with -1 response)
        slot = self.comm.get_current_slot()
        if slot is not None:
            if slot == -1:
                self.current_slot_label.setText("MOVING")
                self.state_label.setText("MOVING")
            else:
                self.current_slot_label.setText(str(slot))
                self.state_label.setText("IDLE")

        # Update calibration status (inferred from num_slots > 0)
        num_slots = self.comm.get_num_slots()
        if num_slots is not None:
            if num_slots > 0:
                self.calibration_status_label.setText("YES")
                self.calibration_status_label.setStyleSheet("color: green;")
                self.calib_status_indicator.setText("Calibrated")
                self.calib_status_indicator.setStyleSheet("color: green;")
                self.slots_detected_label.setText(str(num_slots))
                for i, btn in enumerate(self.slot_buttons):
                    btn.setVisible(i < num_slots)
                if num_slots > 0:
                    self.slot_select_spin.setMaximum(num_slots - 1)
            else:
                self.calibration_status_label.setText("NO")
                self.calibration_status_label.setStyleSheet("color: red;")
                self.calib_status_indicator.setText("Not Calibrated")
                self.calib_status_indicator.setStyleSheet("color: red;")
                self.slots_detected_label.setText("---")
                for btn in self.slot_buttons:
                    btn.setVisible(False)

    def move_to_slot(self, slot: int):
        """Move to specific slot"""
        self.log(f"Moving to slot {slot}...")
        if self.comm.move_to_slot(slot):
            self.log(f"Command sent: Move to slot {slot}")
        else:
            self.log(f"Failed to move to slot {slot}")

    def move_previous(self):
        """Move to previous slot"""
        if not self.comm.is_connected:
            return

        # Get current slot and number of slots
        current = self.comm.get_current_slot()
        num_slots = self.comm.get_num_slots()

        if current is None or num_slots is None:
            self.log("Failed to get current position or slot count")
            return

        # Calculate previous slot (with wraparound)
        prev_slot = (current - 1) if current > 0 else (num_slots - 1)

        self.log(f"Moving to previous slot: {prev_slot}")
        if self.comm.move_to_slot(prev_slot):
            self.log(f"Moving from slot {current} to {prev_slot}")
        else:
            self.log("Failed to move to previous slot")

    def move_next(self):
        """Move to next slot"""
        if not self.comm.is_connected:
            return

        # Get current slot and number of slots
        current = self.comm.get_current_slot()
        num_slots = self.comm.get_num_slots()

        if current is None or num_slots is None:
            self.log("Failed to get current position or slot count")
            return

        # Calculate next slot (with wraparound)
        next_slot = (current + 1) % num_slots

        self.log(f"Moving to next slot: {next_slot}")
        if self.comm.move_to_slot(next_slot):
            self.log(f"Moving from slot {current} to {next_slot}")
        else:
            self.log("Failed to move to next slot")

    def go_home(self):
        """Go to home position"""
        self.log("Going to home position...")
        if self.comm.go_home():
            self.log("Command sent: Go home")
        else:
            self.log("Failed to go home")

    def get_base_offset(self):
        """Get base offset from device"""
        offset = self.comm.get_base_offset()
        if offset is not None:
            self.base_offset_spin.setValue(offset)
            self.log(f"Base offset: {offset}")
        else:
            self.log("Failed to get base offset")

    def set_base_offset(self):
        """Set base offset on device"""
        offset = self.base_offset_spin.value()
        self.log(f"Setting base offset to {offset}...")
        if self.comm.set_base_offset(offset):
            self.log(f"Base offset set to {offset}")
        else:
            self.log("Failed to set base offset")

    def execute_step_move(self):
        """Execute step movement"""
        steps = self.step_spin.value()
        self.log(f"Moving {steps} steps...")
        if self.comm.step_move(steps):
            self.log(f"Step move command sent: {steps}")
        else:
            self.log("Failed to execute step move")

    def send_custom_command(self):
        """Send custom command"""
        cmd = self.custom_cmd_input.text()
        if not cmd:
            return

        self.log(f"Sending: {cmd}")
        response = self.comm.send_command(cmd)
        if response:
            self.log(f"Response: {response}")
        else:
            self.log("No response or error")

    def get_user_offset_for_slot(self):
        """Get user offset for selected slot"""
        if not self.comm.is_connected:
            return

        slot = self.slot_select_spin.value()
        self.log(f"Getting user offset for slot {slot}...")

        offset = self.comm.get_user_offset(slot)
        if offset is not None:
            self.user_offset_spin.setValue(offset)
            self.log(f"Slot {slot} user offset: {offset}")
        else:
            self.log(f"Failed to get user offset for slot {slot}")

    def set_user_offset_for_slot(self):
        """Set user offset for selected slot"""
        if not self.comm.is_connected:
            return

        slot = self.slot_select_spin.value()
        offset = self.user_offset_spin.value()

        self.log(f"Setting user offset for slot {slot} to {offset}...")
        if self.comm.set_user_offset(slot, offset):
            self.log(f"Slot {slot} user offset set to {offset}")
        else:
            self.log(f"Failed to set user offset for slot {slot}")

    def enter_dfu_mode(self):
        """Reboot device into DFU bootloader mode for firmware updates"""
        if not self.comm.is_connected:
            QMessageBox.warning(self, "Not Connected", "Please connect to device first")
            return

        reply = QMessageBox.warning(
            self,
            "Enter DFU Mode",
            "This will reboot the device into DFU bootloader mode.\n\n"
            "The device will disconnect and appear as a DFU device.\n"
            "You can then upload new firmware using DFU tools.\n\n"
            "Continue?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No
        )

        if reply == QMessageBox.Yes:
            self.log("Sending DFU reboot command...")
            if self.comm.reboot_to_dfu():
                self.log("Device rebooting to DFU mode")
                QMessageBox.information(
                    self,
                    "DFU Mode",
                    "Device is rebooting into DFU mode.\n\n"
                    "The device will disconnect and reappear as a DFU device.\n"
                    "You can now upload firmware using:\n"
                    "  pio run -e STM32G0B1 --target upload"
                )
                # Disconnect since device will reset
                self.comm.disconnect()
                self.connection_status.setStyleSheet("color: red; font-size: 20px;")
                self.connect_btn.setText("Connect")
                self.statusBar.showMessage("Disconnected - Device in DFU mode")
                self.status_timer.stop()
            else:
                self.log("Failed to send DFU command")
                QMessageBox.warning(self, "Error", "Failed to send DFU reboot command")

    def log(self, message: str):
        """Add message to log"""
        # Check if log_text exists (it might not during initialization)
        if hasattr(self, 'log_text') and self.log_text is not None:
            self.log_text.append(f"> {message}")

    def closeEvent(self, event):
        """Handle window close event"""
        if self.comm.is_connected:
            self.comm.disconnect()
        event.accept()
