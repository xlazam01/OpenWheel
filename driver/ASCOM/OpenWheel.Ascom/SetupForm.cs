// Setup dialog: choose the wheel, edit filter names and focus offsets, watch the live status and
// health (temperatures, supply, warnings), tune stall detection, and run the few maintenance
// actions (home, halt, clear error, calibrate).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Text;
using System.Windows.Forms;
using OpenWheel.Core;

namespace OpenWheel.Ascom
{
    internal sealed class SetupForm : Form
    {
        private readonly ComboBox cmbDevice = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        private readonly TextBox txtManual = new TextBox();
        private readonly Label lblConnection = new Label { AutoSize = false };
        private readonly Label lblStorage = new Label { AutoSize = false };
        private readonly DataGridView grid = new DataGridView();
        private readonly TextBox txtStatus = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Font = new Font(FontFamily.GenericMonospace, 9f) };
        private readonly Button btnHome = new Button { Text = "Home" };
        private readonly Button btnHalt = new Button { Text = "Halt" };
        private readonly Button btnClear = new Button { Text = "Clear error" };
        private readonly Button btnCalibrate = new Button { Text = "Calibrate..." };
        private readonly ComboBox cmbMove = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        private readonly Button btnMove = new Button { Text = "Move to" };
        private readonly NumericUpDown numStall = new NumericUpDown { Minimum = 0, Maximum = 255 };
        private readonly Button btnStallSave = new Button { Text = "Set" };
        private readonly Button btnStallTest = new Button { Text = "Test move..." };
        private readonly Label lblStall = new Label { AutoSize = false };
        private readonly NumericUpDown numLed = new NumericUpDown { Minimum = 0, Maximum = 100, Increment = 5 };
        private readonly Button btnLedSave = new Button { Text = "Set" };
        private readonly Label lblLed = new Label { AutoSize = false };
        private readonly Button btnOk = new Button { Text = "OK", DialogResult = DialogResult.OK };
        private readonly Button btnCancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel };
        private readonly Timer timer = new Timer { Interval = 400 };

        private List<UsbSerialDevice> candidates = new List<UsbSerialDevice>();
        private int shownSlots = -1;
        private bool acquired;
        private bool stallLoaded;     // threshold read from the wheel once connected
        private int stallTestPhase;   // 0 none, 1 test move running, 2 homing afterwards
        private bool ledLoaded;       // brightness read from the wheel once connected
        private bool ledSupported = true;

        private const int StallTestSteps = 2000;

        public SetupForm()
        {
            Text = "OpenWheel Filter Wheel - setup";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(620, 600);
            AcceptButton = btnOk;
            CancelButton = btnCancel;

            int y = 12;
            Controls.Add(new Label { Text = "Wheel:", Left = 12, Top = y + 3, Width = 110 });
            cmbDevice.SetBounds(125, y, 480, 24);
            Controls.Add(cmbDevice);
            y += 32;
            Controls.Add(new Label { Text = "Manual COM port:", Left = 12, Top = y + 3, Width = 110 });
            txtManual.SetBounds(125, y, 100, 24);
            Controls.Add(txtManual);
            Controls.Add(new Label { Text = "(only if automatic detection does not work; leave empty)", Left = 235, Top = y + 3, Width = 370 });
            y += 32;
            lblConnection.SetBounds(12, y, 596, 36);
            lblConnection.Font = new Font(Font, FontStyle.Bold);
            Controls.Add(lblConnection);
            y += 40;

            Controls.Add(new Label { Text = "Filters (names and focus offsets are offered to the imaging software):", Left = 12, Top = y, Width = 596 });
            y += 20;
            lblStorage.SetBounds(12, y, 596, 18);
            lblStorage.ForeColor = Color.DimGray;
            Controls.Add(lblStorage);
            y += 22;
            grid.SetBounds(12, y, 596, 190);
            grid.AllowUserToAddRows = false;
            grid.AllowUserToDeleteRows = false;
            grid.RowHeadersVisible = false;
            grid.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "Slot", HeaderText = "Slot", ReadOnly = true, Width = 50 });
            grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "FilterName", HeaderText = "Filter name", Width = 330 });
            grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "Offset", HeaderText = "Focus offset (steps)", Width = 180 });
            Controls.Add(grid);
            y += 198;

            Controls.Add(new Label { Text = "Live status:", Left = 12, Top = y, Width = 200 });
            y += 20;
            txtStatus.SetBounds(12, y, 596, 190);
            Controls.Add(txtStatus);
            y += 200;

            Controls.Add(new Label { Text = "Stall detection:", Left = 12, Top = y + 4, Width = 110 });
            numStall.SetBounds(125, y + 1, 60, 24);
            btnStallSave.SetBounds(191, y, 50, 28);
            btnStallTest.SetBounds(247, y, 100, 28);
            lblStall.SetBounds(355, y + 1, 253, 30);
            lblStall.ForeColor = Color.DimGray;
            Controls.AddRange(new Control[] { numStall, btnStallSave, btnStallTest, lblStall });
            new ToolTip().SetToolTip(numStall, "StallGuard threshold (0 = off). A stall stops the move with an error. Use 'Test move' to get a suggestion.");
            y += 34;

            Controls.Add(new Label { Text = "LED brightness %:", Left = 12, Top = y + 4, Width = 110 });
            numLed.SetBounds(125, y + 1, 60, 24);
            btnLedSave.SetBounds(191, y, 50, 28);
            lblLed.SetBounds(247, y + 5, 361, 20);
            lblLed.ForeColor = Color.DimGray;
            Controls.AddRange(new Control[] { numLed, btnLedSave, lblLed });
            new ToolTip().SetToolTip(numLed, "Brightness of the wheel's status LEDs, stored in the wheel. 0 = off (dark site).");
            y += 38;

            btnHome.SetBounds(12, y, 80, 28);
            btnHalt.SetBounds(98, y, 80, 28);
            btnClear.SetBounds(184, y, 90, 28);
            btnCalibrate.SetBounds(280, y, 100, 28);
            cmbMove.SetBounds(400, y + 2, 130, 24);
            btnMove.SetBounds(536, y, 72, 28);
            Controls.AddRange(new Control[] { btnHome, btnHalt, btnClear, btnCalibrate, cmbMove, btnMove });
            y += 42;

            btnOk.SetBounds(448, y, 75, 28);
            btnCancel.SetBounds(530, y, 75, 28);
            Controls.AddRange(new Control[] { btnOk, btnCancel });
            ClientSize = new Size(620, y + 40);

            btnHome.Click += (s, e) => Run(Server.Controller.Home(4000));
            btnHalt.Click += (s, e) => Run(Server.Controller.Halt(4000));
            btnClear.Click += (s, e) => Run(Server.Controller.ClearError(4000));
            btnCalibrate.Click += OnCalibrate;
            btnMove.Click += (s, e) => { if (cmbMove.SelectedIndex >= 0) Run(Server.Controller.MoveToSlot(cmbMove.SelectedIndex, 4000)); };
            btnStallSave.Click += OnStallSave;
            btnStallTest.Click += OnStallTest;
            btnLedSave.Click += OnLedSave;
            btnOk.Click += OnOk;
            timer.Tick += (s, e) => RefreshStatus();

            Load += OnLoad;
            FormClosed += (s, e) =>
            {
                timer.Stop();
                if (acquired) { acquired = false; Server.ReleaseConnection(); }
            };
        }

        private void OnLoad(object sender, EventArgs e)
        {
            LoadDevices();
            txtManual.Text = Settings.ManualPort;
            Server.AcquireConnection();
            acquired = true;
            timer.Start();
            RefreshStatus();
        }

        private void LoadDevices()
        {
            cmbDevice.Items.Clear();
            cmbDevice.Items.Add("Automatic (the OpenWheel found on USB)");
            candidates = DeviceFinder.FindCandidates(new RegistryUsbEnumerator(), null);
            string preferred = Settings.PreferredUsbSerial;
            int select = 0;
            foreach (UsbSerialDevice d in candidates)
            {
                cmbDevice.Items.Add(d.PortName + "   (USB " + d.Vid.ToString("X4") + ":" + d.Pid.ToString("X4") + ", serial " + d.UsbSerial + ")");
                if (!string.IsNullOrEmpty(preferred) && string.Equals(d.UsbSerial, preferred, StringComparison.OrdinalIgnoreCase)) select = cmbDevice.Items.Count - 1;
            }
            cmbDevice.SelectedIndex = select;
        }

        private void RefreshStatus()
        {
            WheelSnapshot s = Server.Controller.Snapshot;
            WheelStatus st = s.Status;

            switch (s.Connection)
            {
                case ConnectionState.Connected:
                    lblConnection.Text = "Connected: " + s.Identity + " on " + s.PortName;
                    lblConnection.ForeColor = Color.DarkGreen;
                    break;
                case ConnectionState.Disconnected:
                    lblConnection.Text = "Not connected";
                    lblConnection.ForeColor = Color.Gray;
                    break;
                default:
                    lblConnection.Text = "Looking for the OpenWheel... " + (s.Problem ?? "");
                    lblConnection.ForeColor = Color.DarkOrange;
                    break;
            }

            var sb = new StringBuilder();
            if (s.IsConnected && st != null)
            {
                sb.AppendLine("State:        " + st.State + (st.Busy ? " (moving)" : ""));
                sb.AppendLine("Position:     " + (s.Position < 0 ? (st.Busy ? "-1 (moving)" : "unknown (home the wheel)") : (s.Position + 1) + " of " + s.SlotCount + "  (slot " + s.Position + ")"));
                sb.AppendLine("Calibrated:   " + (st.Calibrated ? "yes, " + st.Slots + " filters" : "NO"));
                sb.AppendLine("Device error: " + (st.Error == 0 ? "none" : WheelErrors.Describe(st.Error) + " (code " + st.Error + ")"));
                AppendHealth(sb, s);
            }
            if (!string.IsNullOrEmpty(s.Problem)) sb.AppendLine("Note:         " + s.Problem);
            txtStatus.Text = sb.ToString();

            bool ready = s.IsConnected && st != null;
            btnHome.Enabled = ready && !st.Busy;
            btnHalt.Enabled = ready && st.Busy;
            btnClear.Enabled = ready && st.Error != 0;
            btnCalibrate.Enabled = ready && !st.Busy;
            btnMove.Enabled = ready && !st.Busy && cmbMove.SelectedIndex >= 0;
            cmbMove.Enabled = ready && !st.Busy;
            UpdateStall(s, ready);
            UpdateLed(s, ready);

            int slots = ready ? st.Slots : 0;
            bool onWheel = s.FilterConfigSupported;
            lblStorage.Text = !ready ? "" : onWheel ? "Stored in the wheel (its EEPROM): they stay with the wheel on any PC."
                                                    : "This firmware cannot store them: kept on this PC. Update the firmware to store them in the wheel.";
            // with name storage in the wheel wait until they have been read
            bool dataReady = slots > 0 && (!onWheel || s.FilterNames != null);
            if (dataReady && slots != shownSlots) FillGrid(slots, s);
            grid.ReadOnly = !dataReady;
        }

        private static void AppendHealth(StringBuilder sb, WheelSnapshot s)
        {
            if (!s.HousekeepingSupported)
            {
                sb.AppendLine("Health:       not available (firmware older than 1.1)");
                return;
            }
            WheelHousekeeping h = s.Housekeeping;
            if (h == null) return;
            CultureInfo ci = CultureInfo.InvariantCulture;
            sb.AppendLine("MCU temp:     " + h.McuTemperature.ToString("0.0", ci) + " C   (chip, a few degrees above ambient)");
            sb.AppendLine("Driver temp:  " + (h.DriverTemperatureClass == null ? "unknown (driver not answering)"
                                            : h.DriverTemperatureClass == 0 ? "normal (below 120 C)"
                                            : "above " + h.DriverTemperatureClass.Value.ToString(ci) + " C"));
            string supply = "3.3 V rail " + h.Vdda.ToString("0.00", ci) + " V";
            if (h.Supply5V.HasValue)
            {
                supply += ", +5V " + h.Supply5V.Value.ToString("0.00", ci) + " V";
                if (h.Supply5VMin.HasValue) supply += " (lowest during last move " + h.Supply5VMin.Value.ToString("0.00", ci) + " V)";
            }
            sb.AppendLine("Supply:       " + supply);
            sb.AppendLine("Warnings:     " + WheelHousekeeping.Describe(h.Warnings));
        }

        private void UpdateStall(WheelSnapshot s, bool ready)
        {
            bool supported = ready && s.HousekeepingSupported;
            if (supported && !stallLoaded)
            {
                stallLoaded = true;
                StallGuardInfo g;
                if (Server.Controller.ReadStallGuard(out g, 2000).Success)
                {
                    numStall.Value = g.Threshold;
                    lblStall.Text = g.Threshold == 0 ? "Off. Run a test move for a suggestion." : "On.";
                }
                else stallLoaded = false;   // retry on the next refresh
            }
            if (!ready) stallLoaded = false;
            if (ready && !s.HousekeepingSupported) lblStall.Text = "Needs firmware 1.1 or newer.";

            bool idle = supported && !s.Status.Busy;
            numStall.Enabled = idle && stallTestPhase == 0;
            btnStallSave.Enabled = idle && stallTestPhase == 0;
            btnStallTest.Enabled = idle && stallTestPhase == 0 && s.Status.Calibrated;

            if (stallTestPhase == 0 || !ready || s.Status.Busy) return;
            if (stallTestPhase == 1)
            {
                // test move finished: read the load figures, suggest a threshold, then home the wheel
                stallTestPhase = 2;
                StallGuardInfo g;
                if (Server.Controller.ReadStallGuard(out g, 2000).Success && g.Min.HasValue)
                {
                    lblStall.Text = "Load " + g.Min + ".." + g.Max + " (lower = harder). Suggested " + g.SuggestedThreshold + ": press Set.";
                    numStall.Value = g.SuggestedThreshold.Value;
                }
                else lblStall.Text = "No readings from the test move.";
                Run(Server.Controller.Home(4000));
                return;
            }
            stallTestPhase = 0;   // homing after the test finished
        }

        private void UpdateLed(WheelSnapshot s, bool ready)
        {
            if (!ready) { ledLoaded = false; ledSupported = true; }
            else if (!ledLoaded && s.HousekeepingSupported)
            {
                ledLoaded = true;
                int pct;
                CommandResult r = Server.Controller.ReadLedBrightness(out pct, 2000);
                if (r.Success) { numLed.Value = pct; lblLed.Text = pct == 0 ? "LEDs off." : ""; }
                else if (r.ErrorCode == (int)WheelError.UnknownCommand) ledSupported = false;
                else ledLoaded = false;   // retry on the next refresh
            }
            bool usable = ready && s.HousekeepingSupported && ledSupported;
            if (ready && !usable) lblLed.Text = "Needs firmware 1.2 or newer.";
            numLed.Enabled = usable && !s.Status.Busy;
            btnLedSave.Enabled = usable && !s.Status.Busy;
        }

        private void OnLedSave(object sender, EventArgs e)
        {
            int v = (int)numLed.Value;
            CommandResult r = Server.Controller.SetLedBrightness(v, 4000);
            if (r.Success) lblLed.Text = (v == 0 ? "LEDs off" : "Set to " + v + " %") + " (stored in the wheel).";
            Run(r);
        }

        private void OnStallSave(object sender, EventArgs e)
        {
            int v = (int)numStall.Value;
            CommandResult r = Server.Controller.SetStallThreshold(v, 4000);
            if (r.Success) lblStall.Text = v == 0 ? "Off (stored in the wheel)." : "Set to " + v + " (stored in the wheel). Check a few moves.";
            Run(r);
        }

        private void OnStallTest(object sender, EventArgs e)
        {
            DialogResult d = MessageBox.Show(this,
                "The wheel turns " + StallTestSteps + " steps while the motor load is measured, then homes again (about 10 s).\r\n\r\nStart the test move?",
                "OpenWheel", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (d != DialogResult.Yes) return;
            CommandResult r = Server.Controller.MoveSteps(StallTestSteps, 4000);
            if (r.Success) { stallTestPhase = 1; lblStall.Text = "Measuring..."; }
            Run(r);
        }

        private void FillGrid(int slots, WheelSnapshot s)
        {
            shownSlots = slots;
            bool onWheel = s.FilterConfigSupported && s.FilterNames != null && s.FocusOffsets != null;
            string[] names = onWheel ? s.FilterNames : Settings.GetNames(slots);
            int[] offsets = onWheel ? s.FocusOffsets : Settings.GetFocusOffsets(slots);
            grid.Rows.Clear();
            cmbMove.Items.Clear();
            for (int i = 0; i < slots; i++)
            {
                string shown = string.IsNullOrEmpty(names[i]) ? Settings.DefaultName(i) : names[i];
                grid.Rows.Add((i + 1).ToString(CultureInfo.InvariantCulture), names[i], offsets[i].ToString(CultureInfo.InvariantCulture));
                cmbMove.Items.Add((i + 1) + ": " + shown);
            }
            if (cmbMove.Items.Count > 0) cmbMove.SelectedIndex = 0;
        }

        private void Run(CommandResult r)
        {
            if (!r.Success) MessageBox.Show(this, r.Message, "OpenWheel", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            RefreshStatus();
        }

        private void OnCalibrate(object sender, EventArgs e)
        {
            DialogResult d = MessageBox.Show(this,
                "Calibration rotates the wheel several times (about a minute) and replaces the stored filter positions.\r\n\r\nStart calibration?",
                "OpenWheel", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (d == DialogResult.Yes) Run(Server.Controller.Calibrate(4000));
        }

        private void OnOk(object sender, EventArgs e)
        {
            // device choice
            int idx = cmbDevice.SelectedIndex;
            Settings.PreferredUsbSerial = idx > 0 && idx - 1 < candidates.Count ? candidates[idx - 1].UsbSerial : string.Empty;
            Settings.ManualPort = txtManual.Text.Trim();

            // names and offsets
            if (shownSlots > 0 && !grid.ReadOnly)
            {
                grid.EndEdit();
                var names = new List<string>();
                var offsets = new List<int>();
                foreach (DataGridViewRow row in grid.Rows)
                {
                    names.Add(FilterConfigRules.Sanitize(Convert.ToString(row.Cells["FilterName"].Value, CultureInfo.InvariantCulture)));
                    int v;
                    if (!int.TryParse(Convert.ToString(row.Cells["Offset"].Value, CultureInfo.InvariantCulture), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out v) || v < -32768 || v > 32767)
                    {
                        MessageBox.Show(this, "The focus offset of filter " + (offsets.Count + 1) + " is not a whole number between -32768 and 32767.", "OpenWheel", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        DialogResult = DialogResult.None;   // keep the dialog open
                        return;
                    }
                    offsets.Add(v);
                }

                if (Server.Controller.Snapshot.FilterConfigSupported)
                {
                    Cursor = Cursors.WaitCursor;
                    CommandResult r;
                    try { r = Server.Controller.SaveFilterConfig(names.ToArray(), offsets.ToArray()); }
                    finally { Cursor = Cursors.Default; }
                    if (!r.Success)
                    {
                        MessageBox.Show(this, "Could not store the filters in the wheel:\r\n" + r.Message, "OpenWheel", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        DialogResult = DialogResult.None;   // keep the dialog open
                        return;
                    }
                }
                else
                {
                    Settings.SaveNamesAndOffsets(names, offsets);   // older firmware: PC-side store
                }
            }
        }
    }
}
