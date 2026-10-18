// Protocol v2 types and parsers. See docs/PROTOCOL.md. Pure functions, no I/O.
using System;
using System.Globalization;

namespace OpenWheel.Core
{
    /// <summary>Firmware state machine state (field 1 of the status reply).</summary>
    public enum WheelState
    {
        Init = 0,
        Idle = 1,
        Moving = 2,
        Homing = 3,
        Calibrating = 4,
        Error = 5
    }

    /// <summary>Error codes, same numbers as fw/src/error_codes.h.</summary>
    public enum WheelError
    {
        None = 0,
        HomeTimeout = 1,
        MoveTimeout = 2,
        CalibrationFailed = 3,
        Eeprom = 4,
        MotorDriver = 5,
        HallNotSeen = 6,
        BadParameter = 7,
        Busy = 8,
        NotCalibrated = 9,
        UnknownCommand = 10,
        BadFormat = 11,
        NotSupported = 12
    }

    public static class WheelErrors
    {
        /// <summary>True for sticky device faults (codes 1..6), false for command rejections.</summary>
        public static bool IsFault(int code) { return code >= 1 && code <= 6; }

        public static string Describe(int code)
        {
            switch (code)
            {
                case 0: return "no error";
                case 1: return "homing did not finish in time";
                case 2: return "move did not finish in time";
                case 3: return "calibration failed";
                case 4: return "EEPROM missing or write failed";
                case 5: return "motor driver (TMC2209) not answering or reporting overtemperature/short circuit";
                case 6: return "no hall sensor pulse seen while moving (sensor or magnets)";
                case 7: return "bad parameter";
                case 8: return "wheel is busy (moving, homing or calibrating)";
                case 9: return "wheel is not calibrated";
                case 10: return "unknown command (firmware too old?)";
                case 11: return "malformed command";
                case 12: return "setting cannot be changed at runtime";
                default: return "unknown error code " + code.ToString(CultureInfo.InvariantCulture);
            }
        }
    }

    /// <summary>Result of :32# (identify).</summary>
    public sealed class WheelIdentity
    {
        public string Model { get; private set; }
        public string Firmware { get; private set; }
        public int ProtocolVersion { get; private set; }
        public string Serial { get; private set; }

        public const string Signature = "OPENWHEEL";
        public const int MinProtocolVersion = 2;

        /// <summary>Parses "OPENWHEEL,model,firmware,protocol,serial" (without the trailing '#').</summary>
        public static bool TryParse(string value, out WheelIdentity identity)
        {
            identity = null;
            if (string.IsNullOrEmpty(value)) return false;
            string[] p = value.Split(',');
            int proto;
            if (p.Length != 5 || p[0] != Signature) return false;
            if (!int.TryParse(p[3], NumberStyles.None, CultureInfo.InvariantCulture, out proto)) return false;
            if (p[1].Length == 0 || p[2].Length == 0 || p[4].Length == 0) return false;
            identity = new WheelIdentity { Model = p[1], Firmware = p[2], ProtocolVersion = proto, Serial = p[4] };
            return true;
        }

        public bool IsSupported { get { return ProtocolVersion >= MinProtocolVersion; } }

        public override string ToString()
        {
            return Model + " " + Firmware + " (protocol " + ProtocolVersion + ", serial " + Serial + ")";
        }
    }

    /// <summary>Result of :33# (status).</summary>
    public sealed class WheelStatus
    {
        public WheelState State { get; private set; }
        /// <summary>Current slot, -1 while moving or when the position is unknown.</summary>
        public int Slot { get; private set; }
        /// <summary>Target of a slot move, else -1.</summary>
        public int Target { get; private set; }
        /// <summary>Slot count found by calibration (0 = not calibrated).</summary>
        public int Slots { get; private set; }
        /// <summary>Sticky error code, 0 = none.</summary>
        public int Error { get; private set; }
        public int Flags { get; private set; }

        public bool Calibrated { get { return (Flags & 1) != 0; } }
        public bool PositionKnown { get { return (Flags & 2) != 0; } }
        public bool ButtonHeldAtBoot { get { return (Flags & 4) != 0; } }
        public bool EepromFault { get { return (Flags & 8) != 0; } }
        public bool Busy { get { return State == WheelState.Moving || State == WheelState.Homing || State == WheelState.Calibrating || State == WheelState.Init; } }

        /// <summary>Parses "S,state,slot,target,slots,error,flags" (without the trailing '#').</summary>
        public static bool TryParse(string value, out WheelStatus status)
        {
            status = null;
            if (string.IsNullOrEmpty(value)) return false;
            string[] p = value.Split(',');
            if (p.Length != 7 || p[0] != "S") return false;
            int[] n = new int[6];
            for (int i = 0; i < 6; i++)
            {
                if (!int.TryParse(p[i + 1], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out n[i])) return false;
            }
            if (n[0] < 0 || n[0] > 5) return false;              // unknown state
            if (n[1] < -1 || n[1] > 255 || n[2] < -1 || n[2] > 255) return false;
            if (n[3] < 0 || n[3] > 32) return false;              // slot count
            if (n[4] < 0 || n[4] > 255 || n[5] < 0 || n[5] > 255) return false;
            status = new WheelStatus
            {
                State = (WheelState)n[0],
                Slot = n[1],
                Target = n[2],
                Slots = n[3],
                Error = n[4],
                Flags = n[5]
            };
            return true;
        }

        public override string ToString()
        {
            return "state=" + State + " slot=" + Slot + " target=" + Target + " slots=" + Slots +
                   " error=" + Error + " flags=" + Flags;
        }
    }

    /// <summary>Filter names and focus offsets as stored in the wheel (commands 36..39).</summary>
    public static class FilterConfigRules
    {
        public const int MaxNameLength = 15;
        public const int MaxSlots = 9;

        /// <summary>0..15 printable ASCII characters, none of , # : |  (same rule as the firmware).</summary>
        public static bool IsValidName(string name)
        {
            if (name == null || name.Length > MaxNameLength) return false;
            foreach (char c in name)
            {
                if (c < 0x20 || c > 0x7E) return false;
                if (c == ',' || c == '#' || c == ':' || c == '|') return false;
            }
            return true;
        }

        /// <summary>Makes a user typed name acceptable: trims, replaces forbidden characters by '-', cuts to 15.</summary>
        public static string Sanitize(string name)
        {
            if (name == null) return string.Empty;
            var sb = new System.Text.StringBuilder();
            foreach (char c in name.Trim())
            {
                if (c < 0x20 || c > 0x7E) continue;
                sb.Append(c == ',' || c == '#' || c == ':' || c == '|' ? '-' : c);
                if (sb.Length == MaxNameLength) break;
            }
            return sb.ToString().Trim();
        }

        /// <summary>Splits "a,b,,d" into exactly <paramref name="slots"/> entries (empty entries kept, padded/cut).</summary>
        public static string[] ParseNames(string value, int slots)
        {
            var result = new string[slots];
            string[] p = value == null ? new string[0] : value.Split(',');
            for (int i = 0; i < slots; i++) result[i] = i < p.Length ? p[i].Trim() : string.Empty;
            return result;
        }

        /// <summary>Parses "0,-35,120" into <paramref name="slots"/> integers; null if any entry is not a number.</summary>
        public static int[] ParseOffsets(string value, int slots)
        {
            var result = new int[slots];
            string[] p = value == null ? new string[0] : value.Split(',');
            for (int i = 0; i < slots; i++)
            {
                if (i >= p.Length) continue;
                int v;
                if (!int.TryParse(p[i], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out v) || v < -32768 || v > 32767) return null;
                result[i] = v;
            }
            return result;
        }
    }

    public enum ReplyKind
    {
        /// <summary>Query result "value#".</summary>
        Value,
        /// <summary>"OK#".</summary>
        Ok,
        /// <summary>"ERR,code#".</summary>
        Error,
        /// <summary>Nothing (or no terminating '#') within the timeout.</summary>
        Timeout,
        /// <summary>Bytes arrived but they are not a plausible reply (too long, binary, control characters).</summary>
        Garbage
    }

    /// <summary>One answer line from the wheel.</summary>
    public sealed class Reply
    {
        public ReplyKind Kind { get; private set; }
        /// <summary>Reply text without the '#', for Value replies.</summary>
        public string Value { get; private set; }
        /// <summary>Error code for Error replies, else 0.</summary>
        public int ErrorCode { get; private set; }

        public bool IsOk { get { return Kind == ReplyKind.Ok; } }
        public bool IsTransportFailure { get { return Kind == ReplyKind.Timeout || Kind == ReplyKind.Garbage; } }

        public static readonly Reply TimeoutReply = new Reply { Kind = ReplyKind.Timeout };
        public static readonly Reply GarbageReply = new Reply { Kind = ReplyKind.Garbage };

        /// <summary>Classifies one line (without the '#').</summary>
        public static Reply Parse(string line)
        {
            if (line == null) return GarbageReply;
            foreach (char c in line)
            {
                if (c < 0x20 || c > 0x7E) return GarbageReply;
            }
            if (line == "OK") return new Reply { Kind = ReplyKind.Ok };
            if (line.StartsWith("ERR,", StringComparison.Ordinal))
            {
                int code;
                if (int.TryParse(line.Substring(4), NumberStyles.None, CultureInfo.InvariantCulture, out code) && code <= 255)
                    return new Reply { Kind = ReplyKind.Error, ErrorCode = code };
                return GarbageReply;
            }
            return new Reply { Kind = ReplyKind.Value, Value = line };
        }

        public override string ToString()
        {
            switch (Kind)
            {
                case ReplyKind.Ok: return "OK";
                case ReplyKind.Error: return "ERR," + ErrorCode;
                case ReplyKind.Value: return Value;
                default: return Kind.ToString();
            }
        }
    }

    /// <summary>Command builders. Commands are at most 46 characters between ':' and '#'.</summary>
    public static class Commands
    {
        public const int MaxCommandBody = 46;

        public const string Ping = ":25#";
        public const string Identify = ":32#";
        public const string Status = ":33#";
        public const string GetError = ":34#";
        public const string ClearError = ":34,0#";
        public const string Halt = ":30#";
        public const string Home = ":20#";
        public const string Calibrate = ":35#";
        public const string GetSlots = ":16#";
        public const string FirmwareName = ":19#";
        public const string FirmwareVersion = ":18#";

        public static string MoveToSlot(int slot)
        {
            if (slot < 0 || slot > 255) throw new ArgumentOutOfRangeException("slot");
            return ":07 " + slot.ToString(CultureInfo.InvariantCulture) + "#";
        }

        public const string GetNames = ":36#";
        public const string GetFocusOffsets = ":38#";

        public static string SetName(int slot, string name)
        {
            return ":37 " + slot.ToString(CultureInfo.InvariantCulture) + "," + name + "#";
        }

        public static string SetFocusOffset(int slot, int offset)
        {
            return ":39 " + slot.ToString(CultureInfo.InvariantCulture) + "," + offset.ToString(CultureInfo.InvariantCulture) + "#";
        }

        public static string MoveSteps(int steps)
        {
            return ":21 " + steps.ToString(CultureInfo.InvariantCulture) + "#";
        }
    }
}
