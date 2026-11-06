// A simulated OpenWheel (protocol v3, identify first) that can misbehave in the ways real devices do.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using OpenWheel.Core;

namespace OpenWheel.Core.Tests
{
    public enum Misbehavior
    {
        None,
        /// <summary>Never answers anything.</summary>
        Silent,
        /// <summary>Answers with binary junk.</summary>
        Garbage,
        /// <summary>Answers "OK" but never sends the terminating '#'.</summary>
        NoTerminator,
        /// <summary>Sends a long stream of characters without '#'.</summary>
        Flood,
        /// <summary>Answers "OK#" to every command, whatever it was.</summary>
        AlwaysOk,
        /// <summary>Replies arrive after ReplyDelayMs (longer than the driver's timeout).</summary>
        Slow
    }

    public sealed class FakeWheel : ILink
    {
        private readonly object gate = new object();
        private readonly Queue<byte> rx = new Queue<byte>();
        private readonly StringBuilder cmd = new StringBuilder();
        private bool inCmd;
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private long releaseAtMs;

        // device model
        public int Slots = 4;
        public int Slot = 0;
        public bool PositionKnown = true;
        public int MoveMs = 300;
        public int ErrorCode = 0;
        public string Serial = "FAKE0001";
        public string Model = "G0B1";
        public string Firmware = "FW1.4.0";
        public int ProtocolVersion = 3;
        public bool IdentifyFirst = true;      // protocol 3: ERR,16 to everything but :25/:32 until :32#
        public bool Identified;                // cleared on Open() (the host opening the port) and by Relock()
        public bool OldFirmware;               // answers ERR,10 to :32#
        public bool StuckMoving;               // a started move never finishes
        public bool NoFilterConfig;            // firmware without name storage: ERR,10 to commands 36..39
        public string[] FilterNames = { "", "", "", "" };
        public int[] FocusOffsets = { 0, 0, 0, 0 };
        public bool DropReplyToMove;           // executes :07 but does not answer it
        public bool NoHousekeeping;            // firmware older than 1.1: ERR,10 to commands 40..42
        public int Warnings;                   // :40# warning bits; also sets STATUS flag bit4
        public string HousekeepingLine = "H,351,3285,-1,-1,0,{0}";
        public int StallThreshold;
        public string StallReadings = "-1,-1,-1";
        public bool NoLedBrightness;           // firmware 1.1: ERR,10 to commands 43/44
        public int LedBrightness = 100;
        public bool Uncalibrated;              // STATUS: flag bit0 clear, 0 slots (fresh board)
        public bool NoHallState;               // firmware 1.2: ERR,10 to command 45
        public bool HallActive;                // :45# live Hall sensor state
        public string HallReply;               // overrides the :45# reply (garbage tests)
        public Misbehavior Mode = Misbehavior.None;
        public int ReplyDelayMs = 0;

        // observation
        public readonly List<string> Received = new List<string>();
        public int HaltCount;
        public int ProtocolViolations;         // commands other than :25/:32 received before :32#

        private int target = -1;
        private long moveEndMs;
        private bool moving;

        private bool open;
        private bool unplugged;
        public string PortName = "COMFAKE";

        public string Name { get { return PortName; } }
        public bool IsOpen { get { return open && !unplugged; } }

        public void Unplug() { lock (gate) { unplugged = true; } }
        public void Replug() { lock (gate) { unplugged = false; open = false; Identified = false; rx.Clear(); } }
        /// <summary>The firmware locks again, as when the host reconfigures the port (DTR, baud) mid-session.</summary>
        public void Relock() { lock (gate) { Identified = false; } }

        public void Open()
        {
            lock (gate)
            {
                if (unplugged) throw new LinkException("device gone");
                open = true;
                Identified = false;   // the CDC requests sent on open lock the real firmware
            }
        }
        public void Close() { lock (gate) { open = false; } }
        public void Dispose() { Close(); }
        public void DiscardInput() { lock (gate) { rx.Clear(); } }

        public void Write(byte[] data)
        {
            lock (gate)
            {
                if (unplugged || !open) throw new LinkException("device gone");
                foreach (byte b in data)
                {
                    char c = (char)b;
                    if (c == ':') { cmd.Clear(); inCmd = true; }
                    else if (c == '#' && inCmd)
                    {
                        inCmd = false;
                        Handle(cmd.ToString());
                    }
                    else if (inCmd) cmd.Append(c);
                }
            }
        }

        public int Read(byte[] buffer, int timeoutMs)
        {
            var sw = Stopwatch.StartNew();
            while (true)
            {
                lock (gate)
                {
                    if (unplugged || !open) throw new LinkException("device gone");
                    if (rx.Count > 0 && clock.ElapsedMilliseconds >= releaseAtMs)
                    {
                        int n = 0;
                        while (n < buffer.Length && rx.Count > 0) buffer[n++] = rx.Dequeue();
                        return n;
                    }
                }
                if (sw.ElapsedMilliseconds >= timeoutMs) return 0;
                System.Threading.Thread.Sleep(1);
            }
        }

        // ---------------------------------------------------------------- device logic

        private void Reply(string text)
        {
            if (Mode == Misbehavior.Silent) return;
            if (Mode == Misbehavior.Garbage) text = "\u0001\u0002ÿ\u0003";
            if (Mode == Misbehavior.AlwaysOk) text = "OK";
            if (Mode == Misbehavior.Flood) text = new string('x', 500);
            string wire = (Mode == Misbehavior.NoTerminator || Mode == Misbehavior.Flood) ? text : text + "#";
            releaseAtMs = clock.ElapsedMilliseconds + (Mode == Misbehavior.Slow ? ReplyDelayMs : 0);
            foreach (char ch in wire) rx.Enqueue((byte)ch);
        }

        private void Settle()
        {
            if (moving && !StuckMoving && clock.ElapsedMilliseconds >= moveEndMs)
            {
                moving = false;
                if (target >= 0) { Slot = target; PositionKnown = true; }
                target = -1;
            }
        }

        private string StatusLine()
        {
            Settle();
            int state = moving ? 2 : 1;
            int flags = (Uncalibrated ? 0 : 1) | (PositionKnown ? 2 : 0) | (Warnings != 0 && !NoHousekeeping ? 16 : 0);
            return string.Format("S,{0},{1},{2},{3},{4},{5}", state, moving ? -1 : Slot, moving ? target : -1, Uncalibrated ? 0 : Slots, ErrorCode, flags);
        }

        private void Handle(string body)
        {
            Received.Add(body);
            Settle();
            if (body == "25") { Reply("OK"); return; }
            if (body == "32")
            {
                if (OldFirmware) { Reply("ERR,10"); return; }
                Reply(string.Format("OPENWHEEL,{0},{1},{2},{3}", Model, Firmware, ProtocolVersion, Serial));
                Identified = true;
                return;
            }
            if (IdentifyFirst && !Identified)
            {
                ProtocolViolations++;
                Reply("ERR,16");
                return;
            }
            if (body == "33") { Reply(StatusLine()); return; }
            if (body == "30") { HaltCount++; moving = false; target = -1; PositionKnown = false; Reply("OK"); return; }
            if (body == "20")
            {
                if (moving) { Reply("ERR,8"); return; }
                moving = true; target = 0; moveEndMs = clock.ElapsedMilliseconds + MoveMs; PositionKnown = false;
                Reply("OK"); return;
            }
            if (body == "34,0") { ErrorCode = 0; Reply("OK"); return; }
            if (body == "36" || body == "37" || body == "38" || body == "39" || body.StartsWith("37 ", StringComparison.Ordinal) || body.StartsWith("39 ", StringComparison.Ordinal))
            {
                if (NoFilterConfig) { Reply("ERR,10"); return; }
                if (body == "36") { Reply(string.Join(",", FilterNames)); return; }
                if (body == "38") { Reply(string.Join(",", Array.ConvertAll(FocusOffsets, v => v.ToString()))); return; }
                int comma = body.IndexOf(',');
                int slot;
                if (comma < 0 || !int.TryParse(body.Substring(2, comma - 2).Trim(), out slot) || slot < 0 || slot >= 4) { Reply("ERR,7"); return; }
                string rest = body.Substring(comma + 1);
                if (body.StartsWith("37", StringComparison.Ordinal)) { FilterNames[slot] = rest.Trim(); Reply("OK"); return; }
                int off;
                if (!int.TryParse(rest, out off)) { Reply("ERR,7"); return; }
                FocusOffsets[slot] = off; Reply("OK"); return;
            }
            if (body == "40" || body == "41" || body.StartsWith("42", StringComparison.Ordinal))
            {
                if (NoHousekeeping) { Reply("ERR,10"); return; }
                if (body == "40") { Reply(string.Format(HousekeepingLine, Warnings)); return; }
                if (body == "41") { Reply("G," + StallThreshold + "," + StallReadings); return; }
                int thr;
                if (!int.TryParse(body.Substring(2).Trim(' ', ','), out thr) || thr < 0 || thr > 255) { Reply("ERR,7"); return; }
                if (moving) { Reply("ERR,8"); return; }
                StallThreshold = thr; Reply("OK"); return;
            }
            if (body == "43" || body.StartsWith("44", StringComparison.Ordinal))
            {
                if (NoHousekeeping || NoLedBrightness) { Reply("ERR,10"); return; }
                if (body == "43") { Reply(LedBrightness.ToString()); return; }
                int pct;
                if (!int.TryParse(body.Substring(2).Trim(' ', ','), out pct) || pct < 0 || pct > 100) { Reply("ERR,7"); return; }
                if (moving) { Reply("ERR,8"); return; }
                LedBrightness = pct; Reply("OK"); return;
            }
            if (body == "45")
            {
                if (NoHousekeeping || NoLedBrightness || NoHallState) { Reply("ERR,10"); return; }
                Reply(HallReply ?? (HallActive ? "1" : "0")); return;
            }
            if (body.StartsWith("21", StringComparison.Ordinal))
            {
                int steps;
                if (!int.TryParse(body.Substring(2).Trim(' ', ','), out steps)) { Reply("ERR,7"); return; }
                if (moving) { Reply("ERR,8"); return; }
                moving = true; target = -1; moveEndMs = clock.ElapsedMilliseconds + MoveMs; PositionKnown = false;
                Reply("OK"); return;
            }
            if (body.StartsWith("07", StringComparison.Ordinal))
            {
                int slot;
                if (!int.TryParse(body.Substring(2).Trim(' ', ','), out slot) || slot < 0 || slot >= Slots) { Reply("ERR,7"); return; }
                if (moving) { Reply("ERR,8"); return; }
                moving = true; target = slot; moveEndMs = clock.ElapsedMilliseconds + MoveMs; PositionKnown = false;
                if (!DropReplyToMove) Reply("OK");
                return;
            }
            Reply("ERR,10");
        }
    }

    public sealed class FakeEnumerator : IUsbSerialEnumerator
    {
        public readonly List<UsbSerialDevice> Devices = new List<UsbSerialDevice>();
        public IList<UsbSerialDevice> Enumerate() { lock (Devices) return new List<UsbSerialDevice>(Devices); }
    }
}
