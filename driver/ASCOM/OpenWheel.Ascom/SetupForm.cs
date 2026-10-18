// Setup dialog: choose the wheel, edit filter names and focus offsets, watch the live status,
// and run the few maintenance actions (home, halt, clear error, calibrate).
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
        private readonly DataGridView grid = new DataGridView();
        private readonly TextBox txtStatus = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Font = new Font(FontFamily.GenericMonospace, 9f) };
        private readonly Button btnHome = new Button { Text = "Home" };
        private readonly Button btnHalt = new Button { Text = "Halt" };
        private readonly Button btnClear = new Button { Text = "Clear error" };
        private readonly Button btnCalibrate = new Button { Text = "Calibrate..." };
        private readonly ComboBox cmbMove = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        private readonly Button btnMove = new Button { Text = "Move to" };
        private readonly Button btnOk = new Button { Text = "OK", DialogResult = DialogResult.OK };
        private readonly Button btnCancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel };
        private readonly Timer timer = new Timer { Interval = 400 };

        private List<UsbSerialDevice> candidates = new List<UsbSerialDevice>();
        private int shownSlots = -1;
        private bool acquired;

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

            Controls.Add(new Label { Text = "Filters (names and focus offsets are used by the imaging software):", Left = 12, Top = y, Width = 596 });
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
            txtStatus.SetBounds(12, y, 596, 120);
            Controls.Add(txtStatus);
            y += 130;

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

            int slots = ready ? st.Slots : 0;
            if (slots != shownSlots && slots > 0) FillGrid(slots);
            grid.ReadOnly = slots <= 0;
        }

        private void FillGrid(int slots)
        {
            shownSlots = slots;
            string[] names = Settings.GetNames(slots);
            int[] offsets = Settings.GetFocusOffsets(slots);
            grid.Rows.Clear();
            cmbMove.Items.Clear();
            for (int i = 0; i < slots; i++)
            {
                grid.Rows.Add((i + 1).ToString(CultureInfo.InvariantCulture), names[i], offsets[i].ToString(CultureInfo.InvariantCulture));
                cmbMove.Items.Add((i + 1) + ": " + names[i]);
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
            if (shownSlots > 0)
            {
                var names = new List<string>();
                var offsets = new List<int>();
                foreach (DataGridViewRow row in grid.Rows)
                {
                    names.Add(Convert.ToString(row.Cells["FilterName"].Value, CultureInfo.InvariantCulture) ?? string.Empty);
                    int v;
                    int.TryParse(Convert.ToString(row.Cells["Offset"].Value, CultureInfo.InvariantCulture), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out v);
                    offsets.Add(v);
                }
                Settings.SaveNamesAndOffsets(names, offsets);
            }
        }
    }
}
