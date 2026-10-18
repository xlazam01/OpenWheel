// Drives the real driver class (without COM registration) through IFilterWheelV3 against a real wheel.
using System;
using System.Diagnostics;
using System.Threading;
using ASCOM.DeviceInterface;

internal static class Program
{
    private static int failures;

    private static void Check(string what, bool ok, string detail = "")
    {
        Console.WriteLine((ok ? "  PASS  " : "  FAIL  ") + what + (detail.Length > 0 ? "   " + detail : ""));
        if (!ok) failures++;
    }

    private static T Expect<T>(Action a) where T : Exception
    {
        try { a(); }
        catch (T ex) { return ex; }
        catch (Exception ex) { Console.WriteLine("    wrong exception: " + ex.GetType().Name + ": " + ex.Message); return null; }
        return null;
    }

    private static int Main()
    {
        Type t = Type.GetType("OpenWheel.Ascom.FilterWheel, OpenWheel.Ascom.Server");
        var fw = (IFilterWheelV3)Activator.CreateInstance(t);

        Check("interface version is 3", fw.InterfaceVersion == 3);
        Check("name/description/driverinfo", fw.Name.Length > 0 && fw.Description.Length > 0 && fw.DriverInfo.Contains("OpenWheel"), fw.DriverVersion);
        Check("not connected: Position throws NotConnectedException", Expect<ASCOM.NotConnectedException>(() => { var p = fw.Position; }) != null);
        Check("not connected: Names throws NotConnectedException", Expect<ASCOM.NotConnectedException>(() => { var n = fw.Names; }) != null);
        Check("not connected: Connected == false", !fw.Connected);

        var sw = Stopwatch.StartNew();
        fw.Connected = true;
        Check("Connected = true succeeds", fw.Connected, sw.ElapsedMilliseconds + " ms");
        short pos = fw.Position;
        string[] names = fw.Names;
        int[] offs = fw.FocusOffsets;
        Check("Names / FocusOffsets have one entry per slot", names.Length > 0 && names.Length == offs.Length, string.Join(", ", names));
        Check("Position is a valid slot or -1", pos >= -1 && pos < names.Length, "position " + pos);
        Console.WriteLine("  info  Action(Status) = " + fw.Action("Status", ""));
        Check("SupportedActions contains Status", fw.SupportedActions.Contains("Status"));
        var ds = fw.DeviceState;
        Check("DeviceState has Position", ds.Count >= 1);

        Check("Position = -5 throws InvalidValueException", Expect<ASCOM.InvalidValueException>(() => fw.Position = -5) != null);
        Check("Position = 99 throws InvalidValueException", Expect<ASCOM.InvalidValueException>(() => fw.Position = 99) != null);
        Check("raw CommandString is refused", Expect<ASCOM.MethodNotImplementedException>(() => fw.CommandString(":25#", true)) != null);
        Check("unknown action is refused", Expect<ASCOM.ActionNotImplementedException>(() => fw.Action("Explode", "")) != null);
        Check("Calibrate without 'confirm' is refused", Expect<ASCOM.InvalidValueException>(() => fw.Action("Calibrate", "")) != null);

        short target = (short)((pos + 1 + names.Length) % names.Length);
        if (pos < 0) target = 1;
        fw.Position = target;
        short right = fw.Position;
        Check("right after setting Position the wheel reports -1 (moving)", right == -1, "read " + right);
        var t0 = Stopwatch.StartNew();
        while (t0.ElapsedMilliseconds < 20000 && fw.Position != target) Thread.Sleep(100);
        Check("wheel arrives at slot " + target, fw.Position == target, t0.ElapsedMilliseconds + " ms");
        fw.Position = target;
        Check("setting the current slot again is a no-op", fw.Position == target);

        // second client instance shares the same connection
        var fw2 = (IFilterWheelV3)Activator.CreateInstance(t);
        fw2.Connect();
        var t1 = Stopwatch.StartNew();
        while (fw2.Connecting && t1.ElapsedMilliseconds < 8000) Thread.Sleep(20);
        Check("second client: async Connect() works and shares the connection", fw2.Connected && fw2.Position == target, t1.ElapsedMilliseconds + " ms");
        fw.Connected = false;
        Check("first client disconnecting does not drop the second", fw2.Connected);
        Check("first client: Position throws NotConnectedException after disconnect", Expect<ASCOM.NotConnectedException>(() => { var p = fw.Position; }) != null);

        fw2.Action("Home", "");
        Thread.Sleep(300);
        Check("Home action starts a move (Position -1)", fw2.Position == -1);
        var t2 = Stopwatch.StartNew();
        while (t2.ElapsedMilliseconds < 20000 && fw2.Position < 0) Thread.Sleep(100);
        Check("Home finishes at slot 0", fw2.Position == 0, t2.ElapsedMilliseconds + " ms");
        fw2.Dispose();
        fw.Dispose();
        Console.WriteLine(failures == 0 ? "ALL CHECKS PASSED" : failures + " CHECK(S) FAILED");
        return failures;
    }
}
