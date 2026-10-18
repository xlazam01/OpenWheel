// Driver settings, stored in the ASCOM Profile store (HKCU, readable by every client bitness).
using System;
using System.Collections.Generic;
using System.Globalization;
using OpenWheel.Core;

namespace OpenWheel.Ascom
{
    internal static class Settings
    {
        private const string NamesKey = "FilterNames";
        private const string OffsetsKey = "FocusOffsets";
        private const string UsbSerialKey = "PreferredUsbSerial";
        private const string ManualPortKey = "ManualPort";

        // Filter names may not contain this separator.
        private const char Sep = '|';

        private static ASCOM.Utilities.Profile OpenProfile()
        {
            return new ASCOM.Utilities.Profile { DeviceType = Server.DeviceType };
        }

        private static string Get(string key, string def)
        {
            try
            {
                using (var p = OpenProfile())
                {
                    string v = p.GetValue(Server.ProgId, key, string.Empty, def);
                    return v ?? def;
                }
            }
            catch { return def; }
        }

        private static void Set(string key, string value)
        {
            using (var p = OpenProfile())
            {
                p.WriteValue(Server.ProgId, key, value ?? string.Empty);
            }
        }

        public static string PreferredUsbSerial
        {
            get { return Get(UsbSerialKey, string.Empty); }
            set { Set(UsbSerialKey, value); }
        }

        public static string ManualPort
        {
            get { return Get(ManualPortKey, string.Empty); }
            set { Set(ManualPortKey, value); }
        }

        public static WheelOptions LoadOptions()
        {
            var o = new WheelOptions();
            string serial = PreferredUsbSerial;
            string port = ManualPort;
            if (!string.IsNullOrWhiteSpace(serial)) o.PreferredUsbSerial = serial.Trim();
            if (!string.IsNullOrWhiteSpace(port)) o.ManualPort = port.Trim();
            return o;
        }

        // ---------------------------------------------------------------- names and focus offsets

        public static string DefaultName(int slot)
        {
            return "Filter " + (slot + 1).ToString(CultureInfo.InvariantCulture);
        }

        /// <summary>Fills empty entries with "Filter N" (for clients that need a name for every slot).</summary>
        public static string[] WithDefaults(string[] names, int count)
        {
            var result = new string[count];
            for (int i = 0; i < count; i++)
            {
                string n = names != null && i < names.Length ? (names[i] ?? string.Empty).Trim() : string.Empty;
                result[i] = n.Length > 0 ? n : DefaultName(i);
            }
            return result;
        }

        /// <summary>PC-side names (fallback for firmware without name storage), padded with "Filter N".</summary>
        public static string[] GetNames(int count)
        {
            string[] stored = Get(NamesKey, string.Empty).Split(Sep);
            var result = new string[count];
            for (int i = 0; i < count; i++)
            {
                string n = i < stored.Length ? stored[i].Trim() : string.Empty;
                result[i] = n.Length > 0 ? n : "Filter " + (i + 1).ToString(CultureInfo.InvariantCulture);
            }
            return result;
        }

        public static int[] GetFocusOffsets(int count)
        {
            string[] stored = Get(OffsetsKey, string.Empty).Split(Sep);
            var result = new int[count];
            for (int i = 0; i < count; i++)
            {
                int v;
                if (i < stored.Length && int.TryParse(stored[i], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out v)) result[i] = v;
            }
            return result;
        }

        public static void SaveNamesAndOffsets(IList<string> names, IList<int> offsets)
        {
            var cleaned = new List<string>();
            foreach (string n in names) cleaned.Add((n ?? string.Empty).Replace(Sep, '/').Trim());
            Set(NamesKey, string.Join(Sep.ToString(), cleaned));
            var o = new List<string>();
            foreach (int v in offsets) o.Add(v.ToString(CultureInfo.InvariantCulture));
            Set(OffsetsKey, string.Join(Sep.ToString(), o));
        }
    }
}
