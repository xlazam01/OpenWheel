// Console probe: detects the wheel by USB descriptor + identify and drives it through the core library.
using System;
using System.Diagnostics;
using System.Threading;
using OpenWheel.Core;

internal static class Program
{
    private static int Main(string[] args)
    {
        var opt = new WheelOptions();
        if (args.Length > 0 && args[0].StartsWith("COM", StringComparison.OrdinalIgnoreCase)) opt.ManualPort = args[0];

        Console.WriteLine("USB serial devices:");
        foreach (var d in new RegistryUsbEnumerator().Enumerate()) Console.WriteLine("  " + d + (OpenWheelUsb.IsCandidate(d) ? "   <- candidate" : ""));

        using (var wheel = new WheelController(opt, null, null))
        {
            wheel.Log += m => Console.WriteLine("  [log] " + m);
            var sw = Stopwatch.StartNew();
            wheel.Connect();
            if (!wheel.WaitConnected(8000)) { Console.WriteLine("NOT CONNECTED: " + wheel.Snapshot.Problem); return 1; }
            WheelSnapshot s = wheel.Snapshot;
            Console.WriteLine("connected in " + sw.ElapsedMilliseconds + " ms on " + s.PortName + ": " + s.Identity);
            Console.WriteLine("status: " + s.Status + "  position=" + s.Position + " slots=" + s.SlotCount);

            int n = s.SlotCount;
            foreach (int target in new[] { 1 % n, 2 % n, 0 })
            {
                CommandResult r = wheel.MoveToSlot(target);
                Console.WriteLine("move to " + target + ": " + r);
                var t = Stopwatch.StartNew();
                int seenMinus = 0;
                while (t.ElapsedMilliseconds < 20000)
                {
                    Thread.Sleep(100);
                    WheelSnapshot c = wheel.Snapshot;
                    if (c.Position == -1) seenMinus++;
                    if (c.IsConnected && !c.IsMoving && c.Position == target) break;
                }
                Console.WriteLine("  arrived: position=" + wheel.Snapshot.Position + " in " + t.ElapsedMilliseconds + " ms (position was -1 in " + seenMinus + " polls)");
            }
            Console.WriteLine("busy test: " + wheel.MoveToSlot(1) + " then " + wheel.MoveToSlot(2));
            Thread.Sleep(300);
            Console.WriteLine("halt: " + wheel.Halt());
            Thread.Sleep(500);
            Console.WriteLine("after halt: " + wheel.Snapshot.Status + " position=" + wheel.Snapshot.Position);
            Console.WriteLine("home: " + wheel.Home() + "  -> position right after the command: " + wheel.Snapshot.Position + ", moving=" + wheel.Snapshot.IsMoving);
            while (!(wheel.Snapshot.IsConnected && !wheel.Snapshot.IsMoving && wheel.Snapshot.Status.State == WheelState.Idle)) Thread.Sleep(100);
            Console.WriteLine("homed: position=" + wheel.Snapshot.Position + " problem=" + (wheel.Snapshot.Problem ?? "none"));
            wheel.Disconnect();
            Thread.Sleep(400);
            Console.WriteLine("disconnected: " + wheel.Snapshot.Connection);
        }
        return 0;
    }
}
