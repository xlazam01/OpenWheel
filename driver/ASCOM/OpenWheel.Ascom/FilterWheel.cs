// The ASCOM FilterWheel driver class (IFilterWheelV3). Thin: all device handling lives in OpenWheel.Core,
// every call here returns quickly from the shared WheelController's cached snapshot.
using System;
using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using ASCOM;
using ASCOM.DeviceInterface;
using OpenWheel.Core;

namespace OpenWheel.Ascom
{
    [ComVisible(true)]
    [Guid("682ff53b-6972-4eb3-a745-2e53e0efa46a")]
    [ProgId("ASCOM.OpenWheel.FilterWheel")]
    [ServedClassName("OpenWheel Filter Wheel")]
    [ClassInterface(ClassInterfaceType.None)]
    public class FilterWheel : IFilterWheelV3
    {
        private const int ConnectTimeoutMs = 10000;

        private readonly object gate = new object();
        private bool connected;       // this instance holds a connection reference
        private bool connecting;
        private Exception connectError;
        private bool disposed;

        public FilterWheel()
        {
            Server.CountObject();
        }

        ~FilterWheel()
        {
            Release();
        }

        public void Dispose()
        {
            Release();
            GC.SuppressFinalize(this);
        }

        private void Release()
        {
            lock (gate)
            {
                if (disposed) return;
                disposed = true;
                if (connected) { connected = false; Server.ReleaseConnection(); }
            }
            Server.UncountObject();
        }

        // ---------------------------------------------------------------- helpers

        private WheelSnapshot RequireConnected()
        {
            if (!connected) throw new NotConnectedException("The OpenWheel is not connected");
            WheelSnapshot s = Server.Controller.Snapshot;
            if (!s.IsConnected)
                throw new NotConnectedException("The connection to the OpenWheel was lost" + (s.Problem != null ? ": " + s.Problem : "") + ". The driver keeps trying to reconnect.");
            return s;
        }

        private static Exception FaultException(WheelSnapshot s)
        {
            int code = s.DeviceError;
            return new DriverException("OpenWheel fault: " + WheelErrors.Describe(code) + " (code " + code + "). Set Position again to retry.");
        }

        // ---------------------------------------------------------------- connection

        public bool Connected
        {
            get
            {
                lock (gate) { if (!connected) return false; }
                return Server.Controller.Snapshot.IsConnected;
            }
            set
            {
                lock (gate)
                {
                    if (value == connected) return;
                    if (!value) { connected = false; Server.ReleaseConnection(); return; }
                    connected = true;
                    Server.AcquireConnection();
                }

                if (!Server.Controller.WaitConnected(ConnectTimeoutMs))
                {
                    string why = Server.Controller.Snapshot.Problem ?? "no OpenWheel found";
                    lock (gate) { if (connected) { connected = false; Server.ReleaseConnection(); } }
                    throw new DriverException("Cannot connect to the OpenWheel: " + why);
                }
            }
        }

        public void Connect()
        {
            lock (gate)
            {
                if (connected || connecting) return;
                connecting = true;
                connectError = null;
                connected = true;
                Server.AcquireConnection();
            }

            Task.Run(() =>
            {
                bool ok = Server.Controller.WaitConnected(ConnectTimeoutMs);
                lock (gate)
                {
                    if (!ok)
                    {
                        string why = Server.Controller.Snapshot.Problem ?? "no OpenWheel found";
                        if (connected) { connected = false; Server.ReleaseConnection(); }
                        connectError = new DriverException("Cannot connect to the OpenWheel: " + why);
                    }
                    connecting = false;
                }
            });
        }

        public void Disconnect()
        {
            lock (gate)
            {
                if (connected) { connected = false; Server.ReleaseConnection(); }
            }
        }

        public bool Connecting
        {
            get
            {
                lock (gate)
                {
                    if (connectError != null)
                    {
                        Exception e = connectError;
                        connectError = null;
                        throw e;
                    }
                    return connecting;
                }
            }
        }

        // ---------------------------------------------------------------- filter wheel

        public short Position
        {
            get
            {
                WheelSnapshot s = RequireConnected();
                WheelStatus st = s.Status;
                if (st == null) return -1;
                bool faulted = st.State == WheelState.Error ||
                               (!st.Busy && !st.PositionKnown && WheelErrors.IsFault(st.Error));
                if (faulted) throw FaultException(s);
                return (short)s.Position;
            }
            set
            {
                WheelSnapshot s = RequireConnected();
                int n = s.SlotCount;
                if (n <= 0) throw new DriverException("The OpenWheel is not calibrated: its number of filters is unknown. Calibrate it from the setup dialog.");
                if (value < 0 || value >= n) throw new InvalidValueException("Position", value.ToString(CultureInfo.InvariantCulture), "0 to " + (n - 1));

                if (WheelErrors.IsFault(s.DeviceError)) Server.Controller.ClearError(1000);   // a new request retries after an old fault

                CommandResult r = Server.Controller.MoveToSlot(value, 4000);
                if (r.Success) return;
                if (r.ErrorCode == (int)WheelError.BadParameter) throw new InvalidValueException("Position", value.ToString(CultureInfo.InvariantCulture), "0 to " + (n - 1));
                WheelStatus st = Server.Controller.Snapshot.Status;
                if (st != null && st.State == WheelState.Error)
                    throw new DriverException("The OpenWheel is recovering from a fault (" + WheelErrors.Describe(st.Error) + "). Try again in a few seconds.");
                throw new DriverException("Cannot move the OpenWheel: " + r.Message);
            }
        }

        public string[] Names
        {
            get
            {
                WheelSnapshot s = RequireConnected();
                if (s.SlotCount <= 0) throw new DriverException("The OpenWheel is not calibrated: its number of filters is unknown.");
                return Settings.GetNames(s.SlotCount);
            }
        }

        public int[] FocusOffsets
        {
            get
            {
                WheelSnapshot s = RequireConnected();
                if (s.SlotCount <= 0) throw new DriverException("The OpenWheel is not calibrated: its number of filters is unknown.");
                return Settings.GetFocusOffsets(s.SlotCount);
            }
        }

        public IStateValueCollection DeviceState
        {
            get
            {
                WheelSnapshot s = RequireConnected();
                var c = new StateValueCollection();
                c.Add("Position", (short)s.Position);
                c.AddUtcDateTime();
                return c;
            }
        }

        // ---------------------------------------------------------------- extra actions

        public ArrayList SupportedActions
        {
            get { return new ArrayList { "Status", "Home", "Halt", "Calibrate", "ClearError" }; }
        }

        public string Action(string actionName, string actionParameters)
        {
            string name = (actionName ?? string.Empty).Trim().ToLowerInvariant();
            if (name == "status") return StatusJson();

            WheelSnapshot s = RequireConnected();
            CommandResult r;
            switch (name)
            {
                case "home": r = Server.Controller.Home(4000); break;
                case "halt": r = Server.Controller.Halt(4000); break;
                case "clearerror": r = Server.Controller.ClearError(4000); break;
                case "calibrate":
                    // calibration rotates the wheel for up to a minute and overwrites the stored positions
                    if (!string.Equals((actionParameters ?? string.Empty).Trim(), "confirm", StringComparison.OrdinalIgnoreCase))
                        throw new InvalidValueException("Action Calibrate needs the parameter \"confirm\"");
                    r = Server.Controller.Calibrate(4000);
                    break;
                default:
                    throw new ActionNotImplementedException("Action " + actionName + " is not supported. Supported: Status, Home, Halt, Calibrate, ClearError");
            }
            if (!r.Success) throw new DriverException(r.Message);
            return "OK";
        }

        private static string StatusJson()
        {
            WheelSnapshot s = Server.Controller.Snapshot;
            WheelStatus st = s.Status;
            var sb = new StringBuilder("{");
            AppendBool(sb, "connected", s.IsConnected);
            AppendStr(sb, "connection", s.Connection.ToString());
            AppendStr(sb, "port", s.PortName);
            AppendStr(sb, "model", s.Identity == null ? null : s.Identity.Model);
            AppendStr(sb, "firmware", s.Identity == null ? null : s.Identity.Firmware);
            AppendStr(sb, "serial", s.Identity == null ? null : s.Identity.Serial);
            AppendStr(sb, "state", st == null ? null : st.State.ToString());
            AppendNum(sb, "position", s.Position);
            AppendNum(sb, "slots", s.SlotCount);
            AppendBool(sb, "moving", s.IsMoving);
            AppendBool(sb, "calibrated", st != null && st.Calibrated);
            AppendBool(sb, "positionKnown", st != null && st.PositionKnown);
            AppendNum(sb, "error", s.DeviceError);
            AppendStr(sb, "errorText", s.DeviceError == 0 ? null : WheelErrors.Describe(s.DeviceError));
            AppendStr(sb, "problem", s.Problem, true);
            sb.Append('}');
            return sb.ToString();
        }

        private static void AppendStr(StringBuilder sb, string key, string value, bool last = false)
        {
            sb.Append('"').Append(key).Append("\":");
            if (value == null) sb.Append("null");
            else
            {
                sb.Append('"');
                foreach (char c in value)
                {
                    if (c == '"' || c == '\\') sb.Append('\\').Append(c);
                    else if (c < 0x20) sb.Append(' ');
                    else sb.Append(c);
                }
                sb.Append('"');
            }
            if (!last) sb.Append(',');
        }

        private static void AppendNum(StringBuilder sb, string key, int value)
        {
            sb.Append('"').Append(key).Append("\":").Append(value.ToString(CultureInfo.InvariantCulture)).Append(',');
        }

        private static void AppendBool(StringBuilder sb, string key, bool value)
        {
            sb.Append('"').Append(key).Append("\":").Append(value ? "true" : "false").Append(',');
        }

        // ---------------------------------------------------------------- raw commands are deliberately not offered

        public void CommandBlind(string command, bool raw) { throw new MethodNotImplementedException("CommandBlind"); }
        public bool CommandBool(string command, bool raw) { throw new MethodNotImplementedException("CommandBool"); }
        public string CommandString(string command, bool raw) { throw new MethodNotImplementedException("CommandString"); }

        // ---------------------------------------------------------------- identity

        public void SetupDialog() { Server.ShowSetupDialog(); }

        public string Description { get { return "OpenWheel motorised filter wheel (USB)"; } }

        public string DriverInfo
        {
            get { return "OpenWheel ASCOM driver " + DriverVersion + ". Open source, https://github.com/xlazam01/OpenWheel"; }
        }

        public string DriverVersion
        {
            get
            {
                Version v = Assembly.GetExecutingAssembly().GetName().Version;
                return v.Major + "." + v.Minor;
            }
        }

        public short InterfaceVersion { get { return 3; } }

        public string Name
        {
            get
            {
                WheelSnapshot s = Server.Controller.Snapshot;
                return s.IsConnected && s.Identity != null ? "OpenWheel (" + s.Identity.Firmware + ")" : "OpenWheel Filter Wheel";
            }
        }
    }
}
