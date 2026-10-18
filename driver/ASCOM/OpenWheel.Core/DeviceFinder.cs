// Stage 1 of detection: find COM ports whose USB descriptor (VID/PID) matches an OpenWheel.
// Stage 2 (the :32# identify handshake) is done by WheelController / Identifier.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO.Ports;
using Microsoft.Win32;

namespace OpenWheel.Core
{
    public sealed class UsbSerialDevice
    {
        public string PortName { get; set; }
        public int Vid { get; set; }
        public int Pid { get; set; }
        /// <summary>USB serial number string (stable across COM renumbering). May be a generated id if the device has none.</summary>
        public string UsbSerial { get; set; }
        public override string ToString()
        {
            return PortName + " (USB " + Vid.ToString("X4") + ":" + Pid.ToString("X4") + ", serial " + UsbSerial + ")";
        }
    }

    public interface IUsbSerialEnumerator
    {
        /// <summary>All USB serial devices currently present.</summary>
        IList<UsbSerialDevice> Enumerate();
    }

    /// <summary>
    /// Enumerates present USB COM ports from the Windows registry (HKLM\SYSTEM\CurrentControlSet\Enum\USB)
    /// intersected with the ports that currently exist, so entries of unplugged devices are ignored.
    /// </summary>
    public sealed class RegistryUsbEnumerator : IUsbSerialEnumerator
    {
        public IList<UsbSerialDevice> Enumerate()
        {
            var result = new List<UsbSerialDevice>();
            HashSet<string> present;
            try { present = new HashSet<string>(SerialPort.GetPortNames(), StringComparer.OrdinalIgnoreCase); }
            catch { return result; }

            try
            {
                using (RegistryKey usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB"))
                {
                    if (usb == null) return result;
                    foreach (string hw in usb.GetSubKeyNames())
                    {
                        int vid, pid;
                        if (!TryParseVidPid(hw, out vid, out pid)) continue;
                        using (RegistryKey hwKey = usb.OpenSubKey(hw))
                        {
                            if (hwKey == null) continue;
                            foreach (string instance in hwKey.GetSubKeyNames())
                            {
                                using (RegistryKey dp = hwKey.OpenSubKey(instance + @"\Device Parameters"))
                                {
                                    string port = dp == null ? null : dp.GetValue("PortName") as string;
                                    if (string.IsNullOrEmpty(port) || !present.Contains(port)) continue;
                                    result.Add(new UsbSerialDevice { PortName = port, Vid = vid, Pid = pid, UsbSerial = instance });
                                }
                            }
                        }
                    }
                }
            }
            catch
            {
                // registry unreadable: report what we have (nothing); callers fall back to manual selection
            }
            return result;
        }

        /// <summary>Parses "VID_1209&amp;PID_2180" (MI_xx composite suffixes are rejected: not a plain CDC device).</summary>
        public static bool TryParseVidPid(string key, out int vid, out int pid)
        {
            vid = pid = 0;
            if (key == null || !key.StartsWith("VID_", StringComparison.OrdinalIgnoreCase)) return false;
            string[] p = key.Split('&');
            if (p.Length != 2 || !p[1].StartsWith("PID_", StringComparison.OrdinalIgnoreCase)) return false;
            return int.TryParse(p[0].Substring(4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out vid) &&
                   int.TryParse(p[1].Substring(4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out pid);
        }
    }

    /// <summary>Known USB IDs of OpenWheel devices (docs/USB_IDS.md).</summary>
    public static class OpenWheelUsb
    {
        public const int Vid = 0x1209;          // pid.codes
        public const int PidOpenWheel = 0x2180; // registered (pull request pending)
        public const int PidTest = 0x0001;      // shared pid.codes test PID, used by development firmware

        public static bool IsCandidate(UsbSerialDevice d)
        {
            return d.Vid == Vid && (d.Pid == PidOpenWheel || d.Pid == PidTest);
        }
    }

    public static class DeviceFinder
    {
        /// <summary>Stage 1: candidate ports by USB descriptor, preferred serial first.</summary>
        public static List<UsbSerialDevice> FindCandidates(IUsbSerialEnumerator enumerator, string preferredUsbSerial)
        {
            var list = new List<UsbSerialDevice>();
            foreach (UsbSerialDevice d in enumerator.Enumerate())
                if (OpenWheelUsb.IsCandidate(d)) list.Add(d);

            list.Sort((a, b) =>
            {
                bool pa = !string.IsNullOrEmpty(preferredUsbSerial) && string.Equals(a.UsbSerial, preferredUsbSerial, StringComparison.OrdinalIgnoreCase);
                bool pb = !string.IsNullOrEmpty(preferredUsbSerial) && string.Equals(b.UsbSerial, preferredUsbSerial, StringComparison.OrdinalIgnoreCase);
                if (pa != pb) return pa ? -1 : 1;
                // production PID before the shared test PID
                if (a.Pid != b.Pid) return a.Pid == OpenWheelUsb.PidOpenWheel ? -1 : 1;
                return string.CompareOrdinal(a.PortName, b.PortName);
            });
            return list;
        }

        /// <summary>
        /// Stage 2: open the port and run :25# / :32#. Returns the identity or null when the port is
        /// not an OpenWheel (silent, wrong answer, old firmware). Never throws, never blocks longer
        /// than a few command timeouts.
        /// </summary>
        public static WheelIdentity Identify(ILink link, int commandTimeoutMs, out string failure)
        {
            failure = null;
            try
            {
                if (!link.IsOpen) link.Open();
                var t = new Transport(link);

                // The wheel may still be booting (USB enumerates before the firmware loop runs): retry the ping.
                bool alive = false;
                for (int i = 0; i < 3 && !alive; i++)
                {
                    Reply r = t.Execute(Commands.Ping, commandTimeoutMs);
                    alive = r.IsOk;
                }
                if (!alive) { failure = "no answer to ping"; return null; }

                Reply id = t.Execute(Commands.Identify, commandTimeoutMs);
                if (id.Kind == ReplyKind.Error && id.ErrorCode == 10) { failure = "firmware too old (no identify command)"; return null; }
                if (id.Kind != ReplyKind.Value) { failure = "bad identify reply: " + id; return null; }

                WheelIdentity identity;
                if (!WheelIdentity.TryParse(id.Value, out identity)) { failure = "not an OpenWheel: " + id.Value; return null; }
                if (!identity.IsSupported) { failure = "protocol version " + identity.ProtocolVersion + " is not supported (need >= " + WheelIdentity.MinProtocolVersion + ")"; return null; }
                return identity;
            }
            catch (Exception ex)
            {
                failure = ex.Message;
                return null;
            }
        }
    }
}
