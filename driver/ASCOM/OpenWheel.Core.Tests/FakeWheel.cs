// A simulated OpenWheel (protocol v2) that can misbehave in the ways real devices do.
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
        public string Firmware = "FW1.0.1";
        public int ProtocolVersion = 2;
        public bool OldFirmware;               // answers ERR,10 to :32#
        public bool StuckMoving;               // a started move never finishes
        public bool DropReplyToMove;           // executes :07 but does not answer it
        public Misbehavior Mode = Misbehavior.None;
        public int ReplyDelayMs = 0;

        // observation
        public readonly List<string> Received = new List<string>();
        public int HaltCount;

        private int target = -1;
        private long moveEndMs;
        private bool moving;

        private bool open;
        private bool unplugged;
        public string PortName = "COMFAKE";

        public string Name { get { return PortName; } }
        public bool IsOpen { get { return open && !unplugged; } }

        public void Unplug() { lock (gate) { unplugged = true; } }
        public void Replug() { lock (gate) { unplugged = false; open = false; rx.Clear(); } }

        public void Open()
        {
            lock (gate)
            {
                if (unplugged) throw new LinkException("device gone");
                open = true;
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
            int flags = 1 | (PositionKnown ? 2 : 0);
            return string.Format("S,{0},{1},{2},{3},{4},{5}", state, moving ? -1 : Slot, moving ? target : -1, Slots, ErrorCode, flags);
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
