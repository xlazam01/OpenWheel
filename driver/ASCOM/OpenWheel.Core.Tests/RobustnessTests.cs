using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Xunit;

namespace OpenWheel.Core.Tests
{
    public class ParserTests
    {
        [Fact]
        public void StatusParsesValidLine()
        {
            WheelStatus s;
            Assert.True(WheelStatus.TryParse("S,2,-1,3,4,0,3", out s));
            Assert.Equal(WheelState.Moving, s.State);
            Assert.Equal(-1, s.Slot);
            Assert.Equal(3, s.Target);
            Assert.Equal(4, s.Slots);
            Assert.True(s.Calibrated);
            Assert.True(s.PositionKnown);
            Assert.True(s.Busy);
        }

        [Theory]
        [InlineData("")]
        [InlineData("S,1,0,-1,4,0")]            // too few fields
        [InlineData("S,1,0,-1,4,0,3,9")]        // too many
        [InlineData("X,1,0,-1,4,0,3")]          // wrong tag
        [InlineData("S,9,0,-1,4,0,3")]          // unknown state
        [InlineData("S,1,abc,-1,4,0,3")]        // not a number
        [InlineData("S,1,999,-1,4,0,3")]        // slot out of range
        [InlineData("S,1,0,-1,99,0,3")]         // absurd slot count
        public void StatusRejectsBadLines(string line)
        {
            WheelStatus s;
            Assert.False(WheelStatus.TryParse(line, out s));
        }

        [Fact]
        public void IdentityParsesAndChecksProtocol()
        {
            WheelIdentity id;
            Assert.True(WheelIdentity.TryParse("OPENWHEEL,G0B1,FW1.0.1,2,207834924B31", out id));
            Assert.Equal("207834924B31", id.Serial);
            Assert.True(id.IsSupported);
            Assert.True(WheelIdentity.TryParse("OPENWHEEL,G0B1,FW0.9,1,AB", out id));
            Assert.False(id.IsSupported);
            Assert.False(WheelIdentity.TryParse("myFilterWheel", out id));
            Assert.False(WheelIdentity.TryParse("OPENWHEEL,G0B1,FW1,x,AB", out id));
        }

        [Fact]
        public void ReplyClassification()
        {
            Assert.Equal(ReplyKind.Ok, Reply.Parse("OK").Kind);
            Assert.Equal(7, Reply.Parse("ERR,7").ErrorCode);
            Assert.Equal(ReplyKind.Garbage, Reply.Parse("ERR,xx").Kind);
            Assert.Equal(ReplyKind.Garbage, Reply.Parse("a\u0001b").Kind);
            Assert.Equal("42", Reply.Parse("42").Value);
        }

        [Fact]
        public void FilterNameRules()
        {
            Assert.True(FilterConfigRules.IsValidName(""));
            Assert.True(FilterConfigRules.IsValidName("H-alpha 7nm"));
            Assert.True(FilterConfigRules.IsValidName("123456789012345"));
            Assert.False(FilterConfigRules.IsValidName("1234567890123456"));    // 16 chars
            Assert.False(FilterConfigRules.IsValidName("a,b"));
            Assert.False(FilterConfigRules.IsValidName("a|b"));
            Assert.False(FilterConfigRules.IsValidName("a:b"));
            Assert.False(FilterConfigRules.IsValidName("a#b"));
            Assert.False(FilterConfigRules.IsValidName("café"));
            Assert.Equal("a-b-c", FilterConfigRules.Sanitize(" a,b|c "));
            Assert.Equal("123456789012345", FilterConfigRules.Sanitize("1234567890123456789"));
            Assert.Equal(new[] { "L", "", "G", "" }, FilterConfigRules.ParseNames("L,,G", 4));
            Assert.Equal(new[] { 0, -35, 120 }, FilterConfigRules.ParseOffsets("0,-35,120", 3));
            Assert.Null(FilterConfigRules.ParseOffsets("0,x", 2));
            Assert.Null(FilterConfigRules.ParseOffsets("0,99999", 2));
        }

        [Fact]
        public void VidPidKeyParsing()
        {
            int vid, pid;
            Assert.True(RegistryUsbEnumerator.TryParseVidPid("VID_1209&PID_2180", out vid, out pid));
            Assert.Equal(0x1209, vid);
            Assert.Equal(0x2180, pid);
            Assert.False(RegistryUsbEnumerator.TryParseVidPid("VID_1209&PID_2180&MI_00", out vid, out pid));
            Assert.False(RegistryUsbEnumerator.TryParseVidPid("ROOT_HUB30", out vid, out pid));
        }

        [Fact]
        public void CommandsAreWithinFirmwareLimit()
        {
            Assert.True(Commands.MoveToSlot(8).Length - 2 <= Commands.MaxCommandBody);
            Assert.Throws<ArgumentOutOfRangeException>(() => Commands.MoveToSlot(-1));
        }
    }

    public class TransportTests
    {
        private static Transport Make(FakeWheel w) { w.Open(); return new Transport(w); }

        [Fact]
        public void NormalPingAnswersOk()
        {
            Assert.True(Make(new FakeWheel()).Execute(Commands.Ping, 200).IsOk);
        }

        [Fact]
        public void SilentDeviceTimesOutInTime()
        {
            var t = Make(new FakeWheel { Mode = Misbehavior.Silent });
            var sw = Stopwatch.StartNew();
            Reply r = t.Execute(Commands.Ping, 150);
            Assert.Equal(ReplyKind.Timeout, r.Kind);
            Assert.InRange(sw.ElapsedMilliseconds, 100, 400);
        }

        [Theory]
        [InlineData(Misbehavior.Garbage)]
        [InlineData(Misbehavior.NoTerminator)]
        [InlineData(Misbehavior.Flood)]
        public void BadReplyIsTransportFailureAndNeverHangs(Misbehavior mode)
        {
            var t = Make(new FakeWheel { Mode = mode });
            var sw = Stopwatch.StartNew();
            Reply r = t.Execute(Commands.Ping, 150);
            Assert.True(r.IsTransportFailure, r.ToString());
            Assert.InRange(sw.ElapsedMilliseconds, 0, 500);
        }

        [Fact]
        public void LateReplyIsNotMistakenForTheNextAnswer()
        {
            var w = new FakeWheel { Mode = Misbehavior.Slow, ReplyDelayMs = 250 };
            var t = Make(w);
            Assert.Equal(ReplyKind.Timeout, t.Execute(Commands.Ping, 100).Kind);   // reply arrives later
            w.Mode = Misbehavior.None;
            Thread.Sleep(300);                                                       // stale "OK#" is now waiting
            Reply r = t.Execute(Commands.Status, 200);                               // must be the status, not the stale OK
            Assert.Equal(ReplyKind.Value, r.Kind);
            Assert.StartsWith("S,", r.Value);
        }

        [Fact]
        public void OverlongCommandIsRefusedBeforeSending()
        {
            var w = new FakeWheel();
            var t = Make(w);
            Assert.Throws<ArgumentException>(() => t.Execute(":" + new string('9', 60) + "#", 100));
            Assert.Empty(w.Received);
        }

        [Fact]
        public void UnpluggedLinkThrowsLinkException()
        {
            var w = new FakeWheel();
            var t = Make(w);
            w.Unplug();
            Assert.Throws<LinkException>(() => t.Execute(Commands.Ping, 100));
        }
    }

    public class ControllerTests : IDisposable
    {
        private readonly List<WheelController> controllers = new List<WheelController>();
        private readonly Dictionary<WheelController, System.Text.StringBuilder> Logs = new Dictionary<WheelController, System.Text.StringBuilder>();
        private string LogOf(WheelController c) { var l = Logs[c]; lock (l) return l.ToString(); }

        public void Dispose() { foreach (var c in controllers) c.Dispose(); }

        private static WheelOptions FastOptions()
        {
            return new WheelOptions
            {
                CommandTimeoutMs = 100,
                PollIdleMs = 40,
                PollBusyMs = 20,
                MaxConsecutiveFailures = 3,
                MoveTimeoutMs = 600,
                ReconnectMinMs = 40,
                ReconnectMaxMs = 160
            };
        }

        private WheelController Make(WheelOptions opt, FakeEnumerator en, Dictionary<string, FakeWheel> wheels)
        {
            var c = new WheelController(opt, port => wheels[port], en);
            controllers.Add(c);
            return c;
        }

        private static FakeEnumerator OneDevice(string port, int pid = OpenWheelUsb.PidOpenWheel)
        {
            var en = new FakeEnumerator();
            en.Devices.Add(new UsbSerialDevice { PortName = port, Vid = OpenWheelUsb.Vid, Pid = pid, UsbSerial = "SER-" + port });
            return en;
        }

        private static bool WaitFor(Func<bool> cond, int ms)
        {
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < ms)
            {
                if (cond()) return true;
                Thread.Sleep(5);
            }
            return cond();
        }

        private static WheelController Connected(ControllerTests t, FakeWheel w, WheelOptions opt = null)
        {
            w.PortName = "COM9";
            var wheels = new Dictionary<string, FakeWheel> { { "COM9", w } };
            WheelController c = t.Make(opt ?? FastOptions(), OneDevice("COM9"), wheels);
            var log = new System.Text.StringBuilder(); c.Log += m => { lock (log) log.AppendLine(m); }; t.Logs[c] = log;
            c.Connect();
            Assert.True(c.WaitConnected(3000), "did not connect: " + c.Snapshot.Problem);
            return c;
        }

        // ------------------------------------------------------------ happy path

        [Fact]
        public void DetectsConnectsAndReportsState()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            Assert.Equal("COM9", c.Snapshot.PortName);
            Assert.Equal("FAKE0001", c.Snapshot.Identity.Serial);
            Assert.Equal(4, c.Snapshot.SlotCount);
            Assert.Equal(0, c.Snapshot.Position);
            Assert.False(c.Snapshot.IsMoving);
        }

        [Fact]
        public void MoveShowsMinusOneImmediatelyThenArrives()
        {
            var w = new FakeWheel { MoveMs = 300 };
            WheelController c = Connected(this, w);
            CommandResult r = c.MoveToSlot(2);
            Assert.True(r.Success, r.Message);
            // the very next read must already say "moving", not the old slot
            Assert.Equal(-1, c.Snapshot.Position);
            Assert.True(c.Snapshot.IsMoving);
            Assert.True(WaitFor(() => c.Snapshot.Position == 2, 2000));
            Assert.False(c.Snapshot.IsMoving);
        }

        [Fact]
        public void MoveToCurrentSlotSendsNothing()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            int before = w.Received.FindAll(s => s.StartsWith("07")).Count;
            Assert.True(c.MoveToSlot(0).Success);
            Assert.Equal(before, w.Received.FindAll(s => s.StartsWith("07")).Count);
        }

        [Fact]
        public void OutOfRangeSlotIsRejectedWithoutSending()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            CommandResult r = c.MoveToSlot(9);
            Assert.False(r.Success);
            Assert.Equal((int)WheelError.BadParameter, r.ErrorCode);
            Assert.DoesNotContain(w.Received, s => s.StartsWith("07"));
        }

        [Fact]
        public void SecondMoveWhileBusyIsRejectedByTheWheel()
        {
            var w = new FakeWheel { MoveMs = 500 };
            WheelController c = Connected(this, w);
            Assert.True(c.MoveToSlot(1).Success);
            CommandResult r = c.MoveToSlot(2);
            Assert.False(r.Success);
            Assert.Equal((int)WheelError.Busy, r.ErrorCode);
        }

        [Fact]
        public void HaltStopsAMoveAndPositionBecomesUnknown()
        {
            var w = new FakeWheel { MoveMs = 2000 };
            WheelController c = Connected(this, w);
            Assert.True(c.MoveToSlot(3).Success);
            Assert.True(c.Halt().Success);
            Assert.True(WaitFor(() => !c.Snapshot.IsMoving, 1000));
            Assert.Equal(-1, c.Snapshot.Position);        // idle but position not known
        }

        [Fact]
        public void DeviceFaultIsReportedWithText()
        {
            var w = new FakeWheel { ErrorCode = 6 };
            WheelController c = Connected(this, w);
            Assert.Equal(6, c.Snapshot.DeviceError);
            Assert.Contains("hall", c.Snapshot.Problem);
            Assert.True(c.ClearError().Success);
            Assert.True(WaitFor(() => c.Snapshot.DeviceError == 0 && c.Snapshot.Problem == null, 1000));
        }

        // ------------------------------------------------------------ filter names stored in the wheel

        [Fact]
        public void NamesAndOffsetsAreReadFromTheWheelOnConnect()
        {
            var w = new FakeWheel { FilterNames = new[] { "L", "Red", "", "Blue" }, FocusOffsets = new[] { 0, -35, 120, 4 } };
            WheelController c = Connected(this, w);
            WheelSnapshot s = c.Snapshot;
            Assert.True(s.FilterConfigSupported);
            Assert.Equal(new[] { "L", "Red", "", "Blue" }, s.FilterNames);
            Assert.Equal(new[] { 0, -35, 120, 4 }, s.FocusOffsets);
        }

        [Fact]
        public void SavingSendsOnlyWhatChangedAndReadsBack()
        {
            var w = new FakeWheel { FilterNames = new[] { "L", "Red", "G", "B" }, FocusOffsets = new[] { 0, 0, 0, 0 } };
            WheelController c = Connected(this, w);
            w.Received.Clear();
            CommandResult r = c.SaveFilterConfig(new[] { "L", "Red", "Green", "B" }, new[] { 0, 0, 25, 0 });
            Assert.True(r.Success, r.Message);
            Assert.Equal(1, w.Received.FindAll(x => x.StartsWith("37 ")).Count);   // only slot 2's name
            Assert.Equal(1, w.Received.FindAll(x => x.StartsWith("39 ")).Count);   // only slot 2's offset
            Assert.Equal("Green", w.FilterNames[2]);
            Assert.Equal(25, w.FocusOffsets[2]);
            Assert.Equal("Green", c.Snapshot.FilterNames[2]);
            Assert.Equal(25, c.Snapshot.FocusOffsets[2]);
        }

        [Fact]
        public void InvalidNamesAreRejectedBeforeAnythingIsSent()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            w.Received.Clear();
            CommandResult r = c.SaveFilterConfig(new[] { "ok", "bad,name", "", "" }, new[] { 0, 0, 0, 0 });
            Assert.False(r.Success);
            Assert.DoesNotContain(w.Received, x => x.StartsWith("37 "));
        }

        [Fact]
        public void SavingIsRefusedWhileTheWheelMoves()
        {
            var w = new FakeWheel { MoveMs = 800 };
            WheelController c = Connected(this, w);
            Assert.True(c.MoveToSlot(2).Success);
            CommandResult r = c.SaveFilterConfig(new[] { "a", "b", "c", "d" }, new[] { 0, 0, 0, 0 });
            Assert.False(r.Success);
            Assert.Contains("moving", r.Message);
        }

        [Fact]
        public void FirmwareWithoutNameStorageStillConnectsAndSaysSo()
        {
            var w = new FakeWheel { NoFilterConfig = true };
            WheelController c = Connected(this, w);
            Assert.True(c.Snapshot.IsConnected);
            Assert.False(c.Snapshot.FilterConfigSupported);
            Assert.Null(c.Snapshot.FilterNames);
            CommandResult r = c.SaveFilterConfig(new[] { "a", "b", "c", "d" }, new[] { 0, 0, 0, 0 });
            Assert.False(r.Success);
            Assert.Contains("firmware", r.Message);
        }

        [Fact]
        public void PreferredUsbSerialPicksTheRightWheel()
        {
            var en = new FakeEnumerator();
            en.Devices.Add(new UsbSerialDevice { PortName = "COM3", Vid = 0x1209, Pid = 0x2180, UsbSerial = "AAA" });
            en.Devices.Add(new UsbSerialDevice { PortName = "COM4", Vid = 0x1209, Pid = 0x2180, UsbSerial = "BBB" });
            var wheels = new Dictionary<string, FakeWheel>
            {
                { "COM3", new FakeWheel { Serial = "AAA", PortName = "COM3" } },
                { "COM4", new FakeWheel { Serial = "BBB", PortName = "COM4" } }
            };
            var opt = FastOptions(); opt.PreferredUsbSerial = "BBB";
            WheelController c = Make(opt, en, wheels);
            c.Connect();
            Assert.True(c.WaitConnected(3000));
            Assert.Equal("COM4", c.Snapshot.PortName);
        }

        // ------------------------------------------------------------ wrong or broken devices at connect time

        [Fact]
        public void NonOpenWheelDevicesAreSkippedAndTheRealOneIsUsed()
        {
            var en = new FakeEnumerator();
            en.Devices.Add(new UsbSerialDevice { PortName = "COM1", Vid = 0x1209, Pid = 0x0001, UsbSerial = "X" }); // test PID, some other gadget
            en.Devices.Add(new UsbSerialDevice { PortName = "COM2", Vid = 0x1209, Pid = 0x0001, UsbSerial = "Y" });
            en.Devices.Add(new UsbSerialDevice { PortName = "COM5", Vid = 0x0483, Pid = 0x5740, UsbSerial = "Z" }); // not a candidate at all
            var wheels = new Dictionary<string, FakeWheel>
            {
                { "COM1", new FakeWheel { Mode = Misbehavior.AlwaysOk, PortName = "COM1" } },   // says OK to everything
                { "COM2", new FakeWheel { PortName = "COM2" } },
                { "COM5", new FakeWheel { PortName = "COM5" } }
            };
            WheelController c = Make(FastOptions(), en, wheels);
            c.Connect();
            Assert.True(c.WaitConnected(3000));
            Assert.Equal("COM2", c.Snapshot.PortName);
            Assert.Empty(wheels["COM5"].Received);   // never even opened
        }

        [Theory]
        [InlineData(Misbehavior.Silent)]
        [InlineData(Misbehavior.Garbage)]
        [InlineData(Misbehavior.NoTerminator)]
        [InlineData(Misbehavior.Flood)]
        [InlineData(Misbehavior.AlwaysOk)]
        public void BrokenDeviceNeverConnectsAndNothingHangs(Misbehavior mode)
        {
            var w = new FakeWheel { Mode = mode };
            var wheels = new Dictionary<string, FakeWheel> { { "COM9", w } };
            WheelController c = Make(FastOptions(), OneDevice("COM9"), wheels);
            c.Connect();
            Assert.False(c.WaitConnected(800));
            Assert.NotNull(c.Snapshot.Problem);
            var sw = Stopwatch.StartNew();
            CommandResult r = c.MoveToSlot(1);            // must answer at once, not wait
            Assert.False(r.Success);
            Assert.InRange(sw.ElapsedMilliseconds, 0, 100);
        }

        [Fact]
        public void OldFirmwareIsRefusedWithAClearReason()
        {
            var w = new FakeWheel { OldFirmware = true };
            var wheels = new Dictionary<string, FakeWheel> { { "COM9", w } };
            WheelController c = Make(FastOptions(), OneDevice("COM9"), wheels);
            c.Connect();
            Assert.False(c.WaitConnected(600));
            Assert.Contains("too old", c.Snapshot.Problem);
        }

        [Fact]
        public void UnsupportedProtocolVersionIsRefused()
        {
            var w = new FakeWheel { ProtocolVersion = 1 };
            var wheels = new Dictionary<string, FakeWheel> { { "COM9", w } };
            WheelController c = Make(FastOptions(), OneDevice("COM9"), wheels);
            c.Connect();
            Assert.False(c.WaitConnected(600));
            Assert.Contains("protocol version", c.Snapshot.Problem);
        }

        [Fact]
        public void WheelThatAppearsLaterIsFoundByTheBackgroundRetry()
        {
            var en = new FakeEnumerator();   // nothing plugged in yet
            var w = new FakeWheel();
            var wheels = new Dictionary<string, FakeWheel> { { "COM9", w } };
            WheelController c = Make(FastOptions(), en, wheels);
            c.Connect();
            Assert.False(c.WaitConnected(300));
            Assert.Contains("no OpenWheel", c.Snapshot.Problem);
            lock (en.Devices) en.Devices.Add(new UsbSerialDevice { PortName = "COM9", Vid = 0x1209, Pid = 0x2180, UsbSerial = "S" });
            Assert.True(c.WaitConnected(2000));
        }

        // ------------------------------------------------------------ failures after connecting

        [Theory]
        [InlineData(Misbehavior.Silent)]
        [InlineData(Misbehavior.Garbage)]
        [InlineData(Misbehavior.NoTerminator)]
        public void DeviceGoingBadAfterConnectIsDroppedInBoundedTime(Misbehavior mode)
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            w.Mode = mode;
            var sw = Stopwatch.StartNew();
            Assert.True(WaitFor(() => !c.Snapshot.IsConnected, 3000), "still 'connected' to a dead device");
            Assert.InRange(sw.ElapsedMilliseconds, 0, 2000);
            Assert.Equal(ConnectionState.Reconnecting, c.Snapshot.Connection);
            Assert.Equal(-1, c.Snapshot.Position);
            Assert.False(c.MoveToSlot(1).Success);
        }

        [Fact]
        public void UnplugIsDetectedAtOnceAndReplugReconnects()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            w.Unplug();
            Assert.True(WaitFor(() => !c.Snapshot.IsConnected, 1000));
            Assert.Contains("lost", c.Snapshot.Problem);
            w.Replug();
            Assert.True(WaitFor(() => c.Snapshot.IsConnected, 3000), "did not reconnect");
            Assert.Equal(0, c.Snapshot.Position);
        }

        [Fact]
        public void RecoversWhenAGarbageDeviceBecomesHealthyAgain()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            w.Mode = Misbehavior.Garbage;
            Assert.True(WaitFor(() => !c.Snapshot.IsConnected, 3000));
            w.Mode = Misbehavior.None;
            Assert.True(WaitFor(() => c.Snapshot.IsConnected, 3000));
        }

        [Fact]
        public void SlowDeviceMakesCommandsFailFastAndNeverBlockTheCaller()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            w.Mode = Misbehavior.Slow; w.ReplyDelayMs = 2000;
            var sw = Stopwatch.StartNew();
            CommandResult r = c.MoveToSlot(1, 400);       // caller allows 400 ms
            Assert.False(r.Success);
            Assert.InRange(sw.ElapsedMilliseconds, 0, 1500);
        }

        [Fact]
        public void LostReplyToMoveIsRecoveredByAskingForStatus()
        {
            var w = new FakeWheel { DropReplyToMove = true, MoveMs = 300 };
            WheelController c = Connected(this, w);
            CommandResult r = c.MoveToSlot(2);
            Assert.True(r.Success, r.Message);
            Assert.True(WaitFor(() => c.Snapshot.Position == 2, 3000), "snapshot: " + c.Snapshot.Status + " conn=" + c.Snapshot.Connection + " problem=" + c.Snapshot.Problem + " | log: " + LogOf(c));
        }

        [Fact]
        public void WatchdogHaltsAWheelThatNeverFinishes()
        {
            var w = new FakeWheel { StuckMoving = true };
            WheelController c = Connected(this, w);          // MoveTimeoutMs = 600
            Assert.True(c.MoveToSlot(1).Success);
            Assert.True(WaitFor(() => w.HaltCount > 0, 3000), "watchdog did not halt the wheel");
            Assert.True(WaitFor(() => c.Snapshot.Problem != null && c.Snapshot.Problem.Contains("halted"), 1000));
        }

        [Fact]
        public void DisconnectAndDisposeDoNotHangEvenIfTheDeviceIsSilent()
        {
            var w = new FakeWheel();
            WheelController c = Connected(this, w);
            w.Mode = Misbehavior.Silent;
            var sw = Stopwatch.StartNew();
            c.Disconnect();
            Assert.True(WaitFor(() => c.Snapshot.Connection == ConnectionState.Disconnected, 2000));
            c.Dispose();
            Assert.InRange(sw.ElapsedMilliseconds, 0, 3000);
        }

        [Fact]
        public void ManyThreadsReadingWhileTheConnectionFlapsNeverBlockOrThrow()
        {
            var w = new FakeWheel { MoveMs = 50 };
            WheelController c = Connected(this, w);
            var errors = new List<Exception>();
            var stop = false;
            var threads = new List<Thread>();
            for (int i = 0; i < 8; i++)
            {
                var t = new Thread(() =>
                {
                    var rnd = new Random();
                    try
                    {
                        while (!stop)
                        {
                            WheelSnapshot s = c.Snapshot;
                            int p = s.Position; bool m = s.IsMoving; int n = s.SlotCount; string pr = s.Problem;
                            if (rnd.Next(10) == 0) c.MoveToSlot(rnd.Next(4), 200);
                            if (rnd.Next(30) == 0) c.Halt(200);
                        }
                    }
                    catch (Exception ex) { lock (errors) errors.Add(ex); }
                });
                t.Start(); threads.Add(t);
            }
            var modes = new[] { Misbehavior.Silent, Misbehavior.None, Misbehavior.Garbage, Misbehavior.None };
            var sw = Stopwatch.StartNew();
            for (int round = 0; round < 12; round++)
            {
                w.Mode = modes[round % modes.Length];
                if (round == 5) { w.Unplug(); Thread.Sleep(100); w.Replug(); }
                Thread.Sleep(120);
            }
            stop = true;
            foreach (var t in threads) Assert.True(t.Join(3000), "a reader thread is stuck");
            Assert.Empty(errors);
        }
    }
}
