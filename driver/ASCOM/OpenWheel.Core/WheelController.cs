// The worker that owns the serial port. Every public member is thread-safe and bounded in time:
// readers get a cached snapshot, command callers wait at most a given time, and a device that
// stays silent, answers garbage or disappears turns into a clear "not connected" state instead
// of a hang.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

namespace OpenWheel.Core
{
    public enum ConnectionState
    {
        Disconnected,
        /// <summary>Looking for the wheel / opening the port for the first time.</summary>
        Connecting,
        Connected,
        /// <summary>Connection lost; retrying in the background.</summary>
        Reconnecting
    }

    public sealed class WheelOptions
    {
        public int CommandTimeoutMs = 500;
        public int PollIdleMs = 1000;
        public int PollBusyMs = 200;
        /// <summary>Consecutive timeouts/garbage replies before the connection is declared lost.</summary>
        public int MaxConsecutiveFailures = 3;
        /// <summary>The firmware gives up a move/home after 30 s; the driver halts the wheel after this.</summary>
        public int MoveTimeoutMs = 45000;
        public int CalibrationTimeoutMs = 120000;
        public int ReconnectMinMs = 1000;
        public int ReconnectMaxMs = 15000;
        public bool AutoReconnect = true;
        /// <summary>Preferred wheel by USB serial number (stable across COM renumbering). Optional.</summary>
        public string PreferredUsbSerial;
        /// <summary>If set, skip USB detection and use this COM port (it must still pass the identify handshake).</summary>
        public string ManualPort;
    }

    /// <summary>Immutable view of the wheel, safe to read from any thread at any time.</summary>
    public sealed class WheelSnapshot
    {
        public ConnectionState Connection { get; internal set; }
        public string PortName { get; internal set; }
        public WheelIdentity Identity { get; internal set; }
        /// <summary>Last good status, null until the first poll succeeded.</summary>
        public WheelStatus Status { get; internal set; }
        public DateTime StatusTimeUtc { get; internal set; }
        /// <summary>Names stored in the wheel (one per slot, empty = not set), or null if not read yet / not supported.</summary>
        public string[] FilterNames { get; internal set; }
        /// <summary>Focus offsets stored in the wheel (one per slot), or null if not read yet / not supported.</summary>
        public int[] FocusOffsets { get; internal set; }
        /// <summary>False when the firmware has no name storage (older firmware); then the driver keeps names on the PC.</summary>
        public bool FilterConfigSupported { get; internal set; }

        /// <summary>Human readable reason for the last problem (lost connection, device fault, watchdog), or null.</summary>
        public string Problem { get; internal set; }

        public bool IsConnected { get { return Connection == ConnectionState.Connected; } }

        /// <summary>Number of filter slots (0 when unknown or not calibrated).</summary>
        public int SlotCount { get { return IsConnected && Status != null ? Status.Slots : 0; } }

        /// <summary>ASCOM style position: slot number, or -1 while moving / unknown / not connected.</summary>
        public int Position
        {
            get
            {
                if (!IsConnected || Status == null) return -1;
                if (Status.State != WheelState.Idle && Status.State != WheelState.Error) return -1;
                return Status.PositionKnown ? Status.Slot : -1;
            }
        }

        public bool IsMoving { get { return IsConnected && Status != null && Status.Busy; } }

        /// <summary>Sticky device fault code (0 = none).</summary>
        public int DeviceError { get { return Status == null ? 0 : Status.Error; } }
    }

    public sealed class CommandResult
    {
        public bool Success { get; private set; }
        /// <summary>Firmware error code when the wheel rejected the command, else 0.</summary>
        public int ErrorCode { get; private set; }
        public string Message { get; private set; }

        public static readonly CommandResult Ok = new CommandResult { Success = true, Message = "ok" };
        public static CommandResult Fail(string message, int errorCode = 0)
        {
            return new CommandResult { Success = false, Message = message, ErrorCode = errorCode };
        }
        public override string ToString() { return Success ? "ok" : Message; }
    }

    public sealed class WheelController : IDisposable
    {
        private sealed class Work
        {
            public Func<CommandResult> Run;
            public readonly ManualResetEventSlim Done = new ManualResetEventSlim(false);
            public CommandResult Result;
            public int State; // 0 pending, 1 running, 2 done, 3 abandoned
        }

        private readonly WheelOptions opt;
        private readonly Func<string, ILink> linkFactory;
        private readonly IUsbSerialEnumerator enumerator;

        private readonly object queueLock = new object();
        private readonly LinkedList<Work> queue = new LinkedList<Work>();
        private readonly AutoResetEvent wake = new AutoResetEvent(false);
        private Thread thread;
        private volatile bool stop;
        private volatile bool wantConnected;
        private volatile WheelSnapshot snapshot = new WheelSnapshot { Connection = ConnectionState.Disconnected };

        // worker-thread-only state
        private ILink link;
        private Transport transport;
        private int consecutiveFailures;
        private int backoffMs;
        private long nextConnectAttemptMs;
        private long nextPollMs;
        private long busySinceMs = -1;
        private bool watchdogTripped;
        private string problem;
        private string[] cfgNames;
        private int[] cfgOffsets;
        private int cfgSlots = -1;           // slot count the cached config belongs to
        private bool cfgSupported = true;    // false once the wheel answered ERR,10 to :36#
        private long cfgNextTryMs;
        private string stickyProblem;   // e.g. watchdog halt; survives status polls until cleared by the next successful command

        private static readonly long start = Stopwatch.GetTimestamp();
        private static long NowMs() { return (Stopwatch.GetTimestamp() - start) * 1000 / Stopwatch.Frequency; }

        public event Action<WheelSnapshot> SnapshotChanged;
        public event Action<string> Log;

        public WheelController(WheelOptions options, Func<string, ILink> linkFactory, IUsbSerialEnumerator enumerator)
        {
            this.opt = options ?? new WheelOptions();
            this.linkFactory = linkFactory ?? (name => new SerialLink(name));
            this.enumerator = enumerator ?? new RegistryUsbEnumerator();
            backoffMs = opt.ReconnectMinMs;
        }

        public WheelSnapshot Snapshot { get { return snapshot; } }

        // ------------------------------------------------------------------ lifecycle

        public void Start()
        {
            if (thread != null) return;
            thread = new Thread(WorkerMain) { IsBackground = true, Name = "OpenWheel worker" };
            thread.Start();
        }

        /// <summary>Asks the worker to find and open the wheel. Returns immediately; watch <see cref="Snapshot"/>.</summary>
        public void Connect()
        {
            Start();
            wantConnected = true;
            nextConnectAttemptMs = 0;
            backoffMs = opt.ReconnectMinMs;
            wake.Set();
        }

        public void Disconnect()
        {
            wantConnected = false;
            wake.Set();
        }

        /// <summary>Blocks up to <paramref name="timeoutMs"/> until connected (or the time is up). Returns IsConnected.</summary>
        public bool WaitConnected(int timeoutMs)
        {
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < timeoutMs)
            {
                if (snapshot.IsConnected) return true;
                Thread.Sleep(10);
            }
            return snapshot.IsConnected;
        }

        public void Dispose()
        {
            stop = true;
            wantConnected = false;
            wake.Set();
            Thread t = thread;
            if (t != null) t.Join(2000);
        }

        // ------------------------------------------------------------------ commands (bounded wait)

        public CommandResult MoveToSlot(int slot, int waitMs = 3000)
        {
            return Submit(() => DoMove(slot), waitMs, false);
        }

        public CommandResult Home(int waitMs = 3000)
        {
            return Submit(() => SendAction(Commands.Home), waitMs, false);
        }

        public CommandResult Calibrate(int waitMs = 3000)
        {
            return Submit(() => SendAction(Commands.Calibrate), waitMs, false);
        }

        public CommandResult ClearError(int waitMs = 3000)
        {
            return Submit(() => SendAction(Commands.ClearError), waitMs, false);
        }

        /// <summary>Halt jumps the queue.</summary>
        public CommandResult Halt(int waitMs = 3000)
        {
            return Submit(() => SendAction(Commands.Halt), waitMs, true);
        }

        private CommandResult Submit(Func<CommandResult> run, int waitMs, bool urgent)
        {
            if (!snapshot.IsConnected) return CommandResult.Fail("wheel is not connected");
            var w = new Work { Run = run };
            lock (queueLock)
            {
                if (urgent) queue.AddFirst(w); else queue.AddLast(w);
            }
            wake.Set();

            if (w.Done.Wait(waitMs)) return w.Result;

            // Not finished in time: if it has not started, cancel it so it can never run late.
            if (Interlocked.CompareExchange(ref w.State, 3, 0) == 0)
                return CommandResult.Fail("wheel did not respond in time");

            // It is running right now; the worker is bounded by the command timeout, wait for it.
            if (w.Done.Wait(opt.CommandTimeoutMs * 3 + 200)) return w.Result;
            return CommandResult.Fail("wheel did not respond in time");
        }

        // ------------------------------------------------------------------ worker

        private void WorkerMain()
        {
            while (!stop)
            {
                try { Step(); }
                catch (Exception ex)
                {
                    LogMsg("internal error: " + ex);
                    try { LoseConnection("internal error: " + ex.Message); } catch { }
                    Thread.Sleep(200);
                }
            }
            CloseLink();
            FailAllQueued("driver stopped");
            Publish(ConnectionState.Disconnected, null, null, problem);
        }

        private void Step()
        {
            WheelSnapshot s = snapshot;

            if (!wantConnected)
            {
                if (s.Connection != ConnectionState.Disconnected)
                {
                    CloseLink();
                    FailAllQueued("disconnected");
                    problem = null;
                    Publish(ConnectionState.Disconnected, null, null, null);
                }
                wake.WaitOne(200);
                return;
            }

            if (s.Connection == ConnectionState.Connected)
            {
                ServiceConnected();
                return;
            }

            // Disconnected -> Connecting, or retry while Connecting/Reconnecting
            if (s.Connection == ConnectionState.Disconnected)
                Publish(ConnectionState.Connecting, null, null, null);

            long now = NowMs();
            if (now < nextConnectAttemptMs)
            {
                wake.WaitOne((int)Math.Min(nextConnectAttemptMs - now, 100));
                return;
            }
            TryConnect();
        }

        private void TryConnect()
        {
            var candidates = new List<KeyValuePair<string, string>>(); // port, usb serial
            if (!string.IsNullOrEmpty(opt.ManualPort))
            {
                candidates.Add(new KeyValuePair<string, string>(opt.ManualPort, null));
            }
            else
            {
                foreach (UsbSerialDevice d in DeviceFinder.FindCandidates(enumerator, opt.PreferredUsbSerial))
                    candidates.Add(new KeyValuePair<string, string>(d.PortName, d.UsbSerial));
            }

            string lastFailure = candidates.Count == 0 ? "no OpenWheel found on USB" : null;
            foreach (var c in candidates)
            {
                if (stop || !wantConnected) return;
                ILink l = null;
                try
                {
                    l = linkFactory(c.Key);
                    string failure;
                    WheelIdentity id = DeviceFinder.Identify(l, opt.CommandTimeoutMs, out failure);
                    if (id == null)
                    {
                        lastFailure = c.Key + ": " + failure;
                        LogMsg("rejected " + lastFailure);
                        SafeClose(l);
                        continue;
                    }

                    var t = new Transport(l);
                    Reply r = t.Execute(Commands.Status, opt.CommandTimeoutMs);
                    WheelStatus st;
                    if (r.Kind != ReplyKind.Value || !WheelStatus.TryParse(r.Value, out st))
                    {
                        lastFailure = c.Key + ": no valid status after identify";
                        SafeClose(l);
                        continue;
                    }

                    link = l;
                    transport = t;
                    consecutiveFailures = 0;
                    backoffMs = opt.ReconnectMinMs;
                    busySinceMs = -1;
                    watchdogTripped = false;
                    problem = null;
                    stickyProblem = null;
                    nextPollMs = NowMs() + (st.Busy ? opt.PollBusyMs : opt.PollIdleMs);
                    ResetFilterConfig();
                    ReadFilterConfig(st.Slots);
                    if (transport == null) return;   // connection was lost while reading
                    LogMsg("connected " + l.Name + " " + id + " " + st);
                    Publish(ConnectionState.Connected, id, st, ProblemFor(st));
                    return;
                }
                catch (Exception ex)
                {
                    lastFailure = c.Key + ": " + ex.Message;
                    if (l != null) SafeClose(l);
                }
            }

            problem = lastFailure;
            ScheduleReconnect();
            Publish(snapshot.Connection == ConnectionState.Reconnecting ? ConnectionState.Reconnecting : ConnectionState.Connecting,
                    null, null, problem);
        }

        private void ScheduleReconnect()
        {
            nextConnectAttemptMs = NowMs() + backoffMs;
            backoffMs = Math.Min(backoffMs * 2, opt.ReconnectMaxMs);
        }

        private void ServiceConnected()
        {
            // 1. queued commands first
            Work w = null;
            lock (queueLock)
            {
                while (queue.First != null)
                {
                    Work cand = queue.First.Value;
                    queue.RemoveFirst();
                    if (Interlocked.CompareExchange(ref cand.State, 1, 0) == 0) { w = cand; break; }
                }
            }
            if (w != null)
            {
                CommandResult res;
                try { res = w.Run(); }
                catch (Exception ex) { res = CommandResult.Fail("internal error: " + ex.Message); }
                // Refresh the status BEFORE the caller is released: a client that reads Position right
                // after a successful move command must already see "moving" (-1), not the old slot.
                if (res.Success) stickyProblem = null;   // a command was accepted: the old watchdog note is obsolete
                if (res.Success && snapshot.Connection == ConnectionState.Connected) Poll();
                w.Result = res;
                Interlocked.Exchange(ref w.State, 2);
                w.Done.Set();
                nextPollMs = 0;
                return;
            }

            // 2. poll
            long now = NowMs();
            if (now >= nextPollMs)
            {
                Poll();
                if (snapshot.Connection != ConnectionState.Connected) return;
                WheelStatus st = snapshot.Status;
                nextPollMs = NowMs() + (st != null && st.Busy ? opt.PollBusyMs : opt.PollIdleMs);
                Watchdog(st);
                if (snapshot.Connection == ConnectionState.Connected) MaybeRefreshFilterConfig(st);
            }
            else
            {
                wake.WaitOne((int)Math.Min(nextPollMs - now, 50));
            }
        }

        private void Poll()
        {
            Reply r = Send(Commands.Status);
            if (snapshot.Connection != ConnectionState.Connected) return;   // connection was lost inside Send

            WheelStatus st;
            if (r.Kind == ReplyKind.Value && WheelStatus.TryParse(r.Value, out st))
            {
                Publish(ConnectionState.Connected, snapshot.Identity, st, ProblemFor(st) ?? stickyProblem);
                return;
            }
            if (r.Kind == ReplyKind.Value)
            {
                // a reply that is not a status line: same as garbage
                RegisterFailure("unexpected reply to status: " + r.Value);
                return;
            }
            if (r.Kind == ReplyKind.Error)
            {
                problem = "wheel rejected the status request: " + WheelErrors.Describe(r.ErrorCode);
                Publish(ConnectionState.Connected, snapshot.Identity, snapshot.Status, problem);
            }
        }

        private void Watchdog(WheelStatus st)
        {
            if (st == null || !st.Busy || st.State == WheelState.Init) { busySinceMs = -1; watchdogTripped = false; return; }
            long now = NowMs();
            if (busySinceMs < 0) busySinceMs = now;
            int limit = st.State == WheelState.Calibrating ? opt.CalibrationTimeoutMs : opt.MoveTimeoutMs;
            if (now - busySinceMs > limit && !watchdogTripped)
            {
                watchdogTripped = true;
                LogMsg("watchdog: wheel busy for more than " + limit + " ms, halting");
                Send(Commands.Halt);
                problem = "wheel did not finish in time and was halted by the driver";
                stickyProblem = problem;
                nextPollMs = 0;
                Publish(snapshot.Connection, snapshot.Identity, snapshot.Status, problem);
            }
        }

        // ------------------------------------------------------------------ filter names / focus offsets (stored in the wheel)

        private void ResetFilterConfig()
        {
            cfgNames = null;
            cfgOffsets = null;
            cfgSlots = -1;
            cfgSupported = true;
            cfgNextTryMs = 0;
        }

        /// <summary>Reads names and offsets from the wheel into the cache. Returns true when the cache is valid.</summary>
        private bool ReadFilterConfig(int slots)
        {
            if (slots <= 0) { cfgNames = null; cfgOffsets = null; cfgSlots = -1; return false; }

            Reply n = Send(Commands.GetNames);
            if (transport == null) return false;
            if (n.Kind == ReplyKind.Error && n.ErrorCode == (int)WheelError.UnknownCommand)
            {
                cfgSupported = false;   // firmware without name storage: the driver keeps names on the PC
                cfgNames = null; cfgOffsets = null;
                LogMsg("firmware has no filter name storage");
                return false;
            }
            if (n.Kind != ReplyKind.Value) { cfgNextTryMs = NowMs() + 3000; return false; }

            Reply o = Send(Commands.GetFocusOffsets);
            if (o.Kind != ReplyKind.Value) { cfgNextTryMs = NowMs() + 3000; return false; }

            int[] offsets = FilterConfigRules.ParseOffsets(o.Value, slots);
            if (offsets == null) { cfgNextTryMs = NowMs() + 3000; return false; }

            cfgNames = FilterConfigRules.ParseNames(n.Value, slots);
            cfgOffsets = offsets;
            cfgSlots = slots;
            cfgSupported = true;
            return true;
        }

        /// <summary>Loads the config when it is missing or the wheel was recalibrated to another slot count.</summary>
        private void MaybeRefreshFilterConfig(WheelStatus st)
        {
            if (!cfgSupported || st == null || st.Busy || st.Slots <= 0) return;
            if (cfgNames != null && cfgSlots == st.Slots) return;
            if (NowMs() < cfgNextTryMs) return;
            if (ReadFilterConfig(st.Slots) && snapshot.Connection == ConnectionState.Connected)
                Publish(ConnectionState.Connected, snapshot.Identity, snapshot.Status, snapshot.Problem);
        }

        /// <summary>
        /// Writes names and focus offsets to the wheel (EEPROM). Only entries that differ from the cache are
        /// sent; each takes the wheel about 0.25 s. Not allowed while the wheel moves (the EEPROM write
        /// stalls the firmware loop). Invalid names are rejected before anything is sent.
        /// </summary>
        public CommandResult SaveFilterConfig(string[] names, int[] offsets, int waitMs = 20000)
        {
            if (names == null || offsets == null || names.Length != offsets.Length || names.Length > FilterConfigRules.MaxSlots)
                return CommandResult.Fail("invalid filter configuration");
            foreach (string n in names)
                if (!FilterConfigRules.IsValidName(n)) return CommandResult.Fail("invalid filter name \"" + n + "\" (max 15 characters, none of , # : |)");

            return Submit(() => DoSaveFilterConfig((string[])names.Clone(), (int[])offsets.Clone()), waitMs, false);
        }

        private CommandResult DoSaveFilterConfig(string[] names, int[] offsets)
        {
            WheelStatus st = snapshot.Status;
            if (st == null) return CommandResult.Fail("wheel status unknown");
            if (st.Busy) return CommandResult.Fail("the wheel is moving: wait until it has stopped", (int)WheelError.Busy);
            if (!cfgSupported) return CommandResult.Fail("this firmware cannot store filter names (update the firmware)");

            for (int i = 0; i < names.Length; i++)
            {
                string oldName = cfgNames != null && i < cfgNames.Length ? cfgNames[i] : null;
                if (oldName == null || oldName != names[i])
                {
                    Reply r = Send(Commands.SetName(i, names[i]), 2000);
                    if (snapshot.Connection != ConnectionState.Connected) return CommandResult.Fail("connection lost");
                    if (!r.IsOk) return r.Kind == ReplyKind.Error ? Rejected(r) : CommandResult.Fail("no valid answer from the wheel while saving name " + (i + 1));
                }
                int oldOff = cfgOffsets != null && i < cfgOffsets.Length ? cfgOffsets[i] : int.MinValue;
                if (oldOff != offsets[i])
                {
                    Reply r = Send(Commands.SetFocusOffset(i, offsets[i]), 2000);
                    if (snapshot.Connection != ConnectionState.Connected) return CommandResult.Fail("connection lost");
                    if (!r.IsOk) return r.Kind == ReplyKind.Error ? Rejected(r) : CommandResult.Fail("no valid answer from the wheel while saving offset " + (i + 1));
                }
            }

            // read back what the wheel really stored
            if (!ReadFilterConfig(st.Slots)) return CommandResult.Fail("saved, but reading the values back failed");
            Publish(ConnectionState.Connected, snapshot.Identity, snapshot.Status, snapshot.Problem);
            return CommandResult.Ok;
        }

        // ------------------------------------------------------------------ command implementations (run on the worker)

        private CommandResult DoMove(int slot)
        {
            WheelStatus st = snapshot.Status;
            if (st == null) return CommandResult.Fail("wheel status unknown");
            if (st.Slots <= 0) return CommandResult.Fail("wheel is not calibrated", (int)WheelError.NotCalibrated);
            if (slot < 0 || slot >= st.Slots) return CommandResult.Fail("slot " + slot + " is out of range (0.." + (st.Slots - 1) + ")", (int)WheelError.BadParameter);
            if (st.State == WheelState.Idle && st.PositionKnown && st.Slot == slot) return CommandResult.Ok;  // already there

            Reply r = Send(Commands.MoveToSlot(slot));
            if (snapshot.Connection != ConnectionState.Connected) return CommandResult.Fail("connection lost");
            if (r.IsOk) return CommandResult.Ok;
            if (r.Kind == ReplyKind.Error) return Rejected(r);

            // reply lost or garbled: ask the wheel whether the move actually started
            Reply s = Send(Commands.Status);
            WheelStatus st2;
            if (s.Kind == ReplyKind.Value && WheelStatus.TryParse(s.Value, out st2) &&
                st2.State == WheelState.Moving && st2.Target == slot)
                return CommandResult.Ok;
            return CommandResult.Fail("no valid answer from the wheel");
        }

        private CommandResult SendAction(string command)
        {
            Reply r = Send(command);
            if (snapshot.Connection != ConnectionState.Connected) return CommandResult.Fail("connection lost");
            if (r.IsOk) return CommandResult.Ok;
            if (r.Kind == ReplyKind.Error) return Rejected(r);
            return CommandResult.Fail("no valid answer from the wheel");
        }

        private static CommandResult Rejected(Reply r)
        {
            return CommandResult.Fail("wheel rejected the command: " + WheelErrors.Describe(r.ErrorCode), r.ErrorCode);
        }

        // ------------------------------------------------------------------ transport with failure accounting

        /// <summary>Sends one command. A broken link or too many bad replies in a row ends the connection.</summary>
        private Reply Send(string command, int timeoutMs = 0)
        {
            if (transport == null) return Reply.TimeoutReply;
            Reply r;
            try
            {
                r = transport.Execute(command, timeoutMs > 0 ? timeoutMs : opt.CommandTimeoutMs);
            }
            catch (LinkException ex)
            {
                LoseConnection("connection to the wheel lost: " + ex.Message);
                return Reply.TimeoutReply;
            }

            if (r.IsTransportFailure) RegisterFailure(r.Kind == ReplyKind.Timeout ? "wheel did not answer" : "wheel sent garbage");
            else consecutiveFailures = 0;
            return r;
        }

        private void RegisterFailure(string what)
        {
            consecutiveFailures++;
            LogMsg(what + " (" + consecutiveFailures + "/" + opt.MaxConsecutiveFailures + ")");
            if (consecutiveFailures >= opt.MaxConsecutiveFailures)
                LoseConnection(what + " " + consecutiveFailures + " times in a row");
        }

        private void LoseConnection(string why)
        {
            LogMsg("connection lost: " + why);
            CloseLink();
            FailAllQueued("connection lost");
            problem = why;
            busySinceMs = -1;
            if (wantConnected && opt.AutoReconnect)
            {
                ScheduleReconnect();
                Publish(ConnectionState.Reconnecting, snapshot.Identity, null, why);
            }
            else
            {
                wantConnected = false;
                Publish(ConnectionState.Disconnected, null, null, why);
            }
        }

        // ------------------------------------------------------------------ helpers

        private static string ProblemFor(WheelStatus st)
        {
            if (st == null) return null;
            if (st.Error != 0) return "wheel reports: " + WheelErrors.Describe(st.Error) + " (code " + st.Error + ")";
            if (st.EepromFault) return "wheel reports an EEPROM fault";
            return null;
        }

        private void FailAllQueued(string why)
        {
            lock (queueLock)
            {
                foreach (Work w in queue)
                {
                    if (Interlocked.CompareExchange(ref w.State, 2, 0) == 0)
                    {
                        w.Result = CommandResult.Fail(why);
                        w.Done.Set();
                    }
                }
                queue.Clear();
            }
        }

        private void CloseLink()
        {
            ILink l = link;
            link = null;
            transport = null;
            if (l != null) SafeClose(l);
        }

        private static void SafeClose(ILink l)
        {
            try { l.Close(); } catch { }
            try { l.Dispose(); } catch { }
        }

        private void Publish(ConnectionState conn, WheelIdentity id, WheelStatus st, string prob)
        {
            var s = new WheelSnapshot
            {
                Connection = conn,
                PortName = conn == ConnectionState.Connected && link != null ? link.Name : (conn == ConnectionState.Reconnecting || conn == ConnectionState.Connecting ? snapshot.PortName : null),
                Identity = id,
                Status = st,
                StatusTimeUtc = st == null ? DateTime.MinValue : DateTime.UtcNow,
                Problem = prob,
                FilterNames = cfgNames,
                FocusOffsets = cfgOffsets,
                FilterConfigSupported = cfgSupported
            };
            snapshot = s;
            Action<WheelSnapshot> h = SnapshotChanged;
            if (h != null)
            {
                try { h(s); } catch { }
            }
        }

        private void LogMsg(string m)
        {
            Action<string> h = Log;
            if (h != null)
            {
                try { h(m); } catch { }
            }
        }
    }
}
