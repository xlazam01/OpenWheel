// COM LocalServer for the OpenWheel ASCOM driver.
//
// One process serves every client (NINA, PHD2, ...), so all of them share one WheelController
// and one serial connection. The mechanics (class factory, registration, shut-down when the last
// client has gone) follow the ASCOM LocalServer template, written compactly.
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
using OpenWheel.Core;

namespace OpenWheel.Ascom
{
    internal static class Server
    {
        public const string ProgId = "ASCOM.OpenWheel.FilterWheel";
        public const string Description = "OpenWheel Filter Wheel";
        public const string DeviceType = "FilterWheel";
        public static readonly Guid Clsid = new Guid("682ff53b-6972-4eb3-a745-2e53e0efa46a");
        public static readonly Guid AppId = new Guid("dde022df-4b59-4401-88e2-e75cd01ca337");

        // ------------------------------------------------------------------ shared state

        private static readonly object gate = new object();
        private static int objectCount;
        private static int lockCount;
        private static int connectionClients;
        private static WheelController controller;
        private static readonly ManualResetEvent exitEvent = new ManualResetEvent(false);
        public static bool StartedByCom { get; private set; }

        /// <summary>The single controller shared by all driver instances in this process.</summary>
        public static WheelController Controller
        {
            get
            {
                lock (gate)
                {
                    if (controller == null)
                    {
                        controller = new WheelController(Settings.LoadOptions(), null, null);
                        controller.Log += DriverLog.Write;
                        controller.Start();
                    }
                    return controller;
                }
            }
        }

        /// <summary>A client wants the wheel connected. Returns the new client count.</summary>
        public static void AcquireConnection()
        {
            lock (gate)
            {
                connectionClients++;
                if (connectionClients == 1)
                {
                    // settings may have changed in the setup dialog since the last connection
                    ApplyOptions();
                }
            }
            Controller.Connect();
        }

        public static void ReleaseConnection()
        {
            lock (gate)
            {
                if (connectionClients > 0) connectionClients--;
                if (connectionClients == 0 && controller != null) controller.Disconnect();
            }
        }

        private static void ApplyOptions()
        {
            // The controller reads its options at construction: rebuild it when nobody is connected.
            WheelController old = controller;
            controller = null;
            if (old != null) { old.Dispose(); }
            // next access creates a new controller with fresh options
        }

        public static void CountObject() { Interlocked.Increment(ref objectCount); }
        public static void UncountObject() { Interlocked.Decrement(ref objectCount); }
        public static void Lock() { Interlocked.Increment(ref lockCount); }
        public static void Unlock() { Interlocked.Decrement(ref lockCount); }

        private static void ExitIfIdle()
        {
            if (!StartedByCom) return;
            if (Volatile.Read(ref objectCount) <= 0 && Volatile.Read(ref lockCount) <= 0) exitEvent.Set();
        }

        // ------------------------------------------------------------------ entry point

        [MTAThread]
        private static int Main(string[] args)
        {
            string arg = args.Length > 0 ? args[0].ToLowerInvariant() : "";
            bool silent = args.Length > 1 && args[1].ToLowerInvariant() == "/silent";   // used by the installer: no message boxes
            try
            {
                switch (arg)
                {
                    case "-embedding":
                    case "/embedding":
                        return RunComServer();

                    case "/regserver":
                    case "-regserver":
                    case "/register":
                    case "-register":
                        return Elevated(arg, silent, () => { Register(); return 0; }, "registered");

                    case "/unregserver":
                    case "-unregserver":
                    case "/unregister":
                    case "-unregister":
                        return Elevated(arg, silent, () => { Unregister(); return 0; }, "unregistered");

                    case "":
                    case "/setup":
                        ShowSetupOnStaThread();
                        WheelController standalone;
                        lock (gate) { standalone = controller; controller = null; }
                        if (standalone != null) standalone.Dispose();
                        return 0;

                    default:
                        Message("Unknown argument: " + arg + "\r\n\r\nUse /regserver or /unregserver (administrator rights are requested automatically), or no argument to open the setup dialog.");
                        return 2;
                }
            }
            catch (Exception ex)
            {
                DriverLog.Write("fatal: " + ex);
                Message("OpenWheel driver error: " + ex.Message);
                return 1;
            }
        }

        private static int RunComServer()
        {
            StartedByCom = true;
            DriverLog.Write("COM server starting");
            var factory = new FilterWheelFactory();
            uint cookie;
            Guid clsid = Clsid;
            int hr = CoRegisterClassObject(ref clsid, factory, 4 /*LOCAL_SERVER*/, 1 | 4 /*MULTIPLEUSE|SUSPENDED*/, out cookie);
            if (hr != 0) { DriverLog.Write("CoRegisterClassObject failed: 0x" + hr.ToString("X8")); return 3; }
            CoResumeClassObjects();

            // Periodically collect so that released COM objects get finalized and counted down,
            // then leave when no client uses the server any more.
            var idleSince = Stopwatch.StartNew();
            while (!exitEvent.WaitOne(2000))
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                if (Volatile.Read(ref objectCount) > 0 || Volatile.Read(ref lockCount) > 0) idleSince.Restart();
                else if (idleSince.ElapsedMilliseconds > 4000) break;   // idle for 4 s: leave
            }

            CoRevokeClassObject(cookie);
            WheelController c;
            lock (gate) { c = controller; controller = null; }
            if (c != null) c.Dispose();
            DriverLog.Write("COM server stopped");
            return 0;
        }

        private static void ShowSetupOnStaThread()
        {
            var t = new Thread(() =>
            {
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                using (var f = new SetupForm()) f.ShowDialog();
            });
            t.SetApartmentState(ApartmentState.STA);
            t.Start();
            t.Join();
        }

        /// <summary>Shows the setup dialog from a COM call (runs on its own STA thread, blocks until closed).</summary>
        public static void ShowSetupDialog()
        {
            ShowSetupOnStaThread();
        }

        private static void Message(string text)
        {
            MessageBox.Show(text, Description, MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        // ------------------------------------------------------------------ registration

        private static bool IsAdministrator()
        {
            using (WindowsIdentity id = WindowsIdentity.GetCurrent())
                return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
        }

        /// <summary>Runs the action as administrator, asking for elevation (UAC) if necessary.</summary>
        private static int Elevated(string arg, bool silent, Func<int> action, string doneWord)
        {
            if (IsAdministrator())
            {
                int rc = action();
                if (rc == 0 && !silent) Message(Description + " driver " + doneWord + ".");
                return rc;
            }
            var psi = new ProcessStartInfo(Application.ExecutablePath, silent ? arg + " /silent" : arg) { UseShellExecute = true, Verb = "runas" };
            try
            {
                using (Process p = Process.Start(psi))
                {
                    p.WaitForExit();
                    return p.ExitCode;
                }
            }
            catch (System.ComponentModel.Win32Exception)
            {
                if (!silent) Message("Administrator rights are required and were not granted.");
                return 5;
            }
        }

        private static void Register()
        {
            string exe = Application.ExecutablePath;
            string clsid = "{" + Clsid.ToString().ToUpperInvariant() + "}";
            string appid = "{" + AppId.ToString().ToUpperInvariant() + "}";

            // Write both registry views: 32-bit and 64-bit clients each look in their own.
            foreach (RegistryView view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
            {
                using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (RegistryKey classes = hklm.CreateSubKey(@"SOFTWARE\Classes"))
                {
                    using (RegistryKey k = classes.CreateSubKey("AppID\\" + appid)) k.SetValue(null, Description);
                    using (RegistryKey k = classes.CreateSubKey("AppID\\" + Path.GetFileName(exe))) k.SetValue("AppID", appid);

                    using (RegistryKey k = classes.CreateSubKey("CLSID\\" + clsid))
                    {
                        k.SetValue(null, Description);
                        k.SetValue("AppID", appid);
                        using (RegistryKey s = k.CreateSubKey("LocalServer32")) s.SetValue(null, exe);
                        using (RegistryKey s = k.CreateSubKey("ProgId")) s.SetValue(null, ProgId);
                        using (RegistryKey s = k.CreateSubKey("Programmable")) { }
                    }

                    using (RegistryKey k = classes.CreateSubKey(ProgId))
                    {
                        k.SetValue(null, Description);
                        using (RegistryKey s = k.CreateSubKey("CLSID")) s.SetValue(null, clsid);
                    }
                }
            }

            using (var profile = new ASCOM.Utilities.Profile())
            {
                profile.DeviceType = DeviceType;
                profile.Register(ProgId, Description);
            }
        }

        private static void Unregister()
        {
            string clsid = "{" + Clsid.ToString().ToUpperInvariant() + "}";
            string appid = "{" + AppId.ToString().ToUpperInvariant() + "}";
            foreach (RegistryView view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
            {
                using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (RegistryKey classes = hklm.OpenSubKey(@"SOFTWARE\Classes", true))
                {
                    if (classes == null) continue;
                    classes.DeleteSubKeyTree("CLSID\\" + clsid, false);
                    classes.DeleteSubKeyTree(ProgId, false);
                    classes.DeleteSubKeyTree("AppID\\" + appid, false);
                    classes.DeleteSubKeyTree("AppID\\" + Path.GetFileName(Application.ExecutablePath), false);
                }
            }
            using (var profile = new ASCOM.Utilities.Profile())
            {
                profile.DeviceType = DeviceType;
                profile.Unregister(ProgId);
            }
        }

        // ------------------------------------------------------------------ COM imports

        [DllImport("ole32.dll")]
        private static extern int CoRegisterClassObject(ref Guid rclsid, [MarshalAs(UnmanagedType.Interface)] IClassFactory pUnk,
                                                        uint dwClsContext, uint flags, out uint lpdwRegister);

        [DllImport("ole32.dll")]
        private static extern int CoRevokeClassObject(uint dwRegister);

        [DllImport("ole32.dll")]
        private static extern int CoResumeClassObjects();
    }

    [ComImport, Guid("00000001-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IClassFactory
    {
        [PreserveSig] int CreateInstance(IntPtr pUnkOuter, ref Guid riid, out IntPtr ppvObject);
        [PreserveSig] int LockServer([MarshalAs(UnmanagedType.Bool)] bool fLock);
    }

    [ComVisible(true), ClassInterface(ClassInterfaceType.None)]
    internal sealed class FilterWheelFactory : IClassFactory
    {
        private const int CLASS_E_NOAGGREGATION = unchecked((int)0x80040110);

        public int CreateInstance(IntPtr pUnkOuter, ref Guid riid, out IntPtr ppvObject)
        {
            ppvObject = IntPtr.Zero;
            if (pUnkOuter != IntPtr.Zero) return CLASS_E_NOAGGREGATION;
            var obj = new FilterWheel();
            IntPtr unk = Marshal.GetIUnknownForObject(obj);
            try { return Marshal.QueryInterface(unk, ref riid, out ppvObject); }
            finally { Marshal.Release(unk); }
        }

        public int LockServer(bool fLock)
        {
            if (fLock) Server.Lock(); else Server.Unlock();
            return 0;
        }
    }

    /// <summary>Small rolling log in %LOCALAPPDATA%\OpenWheel\driver.log (helps when a client misbehaves).</summary>
    internal static class DriverLog
    {
        private static readonly object gate = new object();
        private static string path;

        public static void Write(string message)
        {
            try
            {
                lock (gate)
                {
                    if (path == null)
                    {
                        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenWheel");
                        Directory.CreateDirectory(dir);
                        path = Path.Combine(dir, "driver.log");
                    }
                    var fi = new FileInfo(path);
                    if (fi.Exists && fi.Length > 512 * 1024)
                    {
                        string old = path + ".1";
                        if (File.Exists(old)) File.Delete(old);
                        File.Move(path, old);
                    }
                    File.AppendAllText(path, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + " " + message + Environment.NewLine);
                }
            }
            catch { }
        }
    }
}
