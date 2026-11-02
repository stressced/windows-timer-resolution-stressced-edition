// TimerSweep - measures real Sleep(1) precision at different timer resolutions.
// See "Re-measuring" in AGENTS.md for the methodology and how to read the output.
//
//   TimerSweep measure <samples>
//       Measure only; this process requests nothing. With global mode on, this is
//       what a game that never calls timeBeginPeriod gets.
//   TimerSweep list <v1,v2,...> <samples> [passes]
//       Request each value (ms) and measure. Passes alternate ascending/descending
//       order, which cancels the order artifacts a single pass produces.
//   TimerSweep sweep <from> <to> <step> <samples>
//       One ascending pass over a range, for locating the tick boundary.
//
// Close ResTimer.exe first: the finest request wins, so while it holds 0.502x every
// coarser value in a sweep silently runs at 0.502x instead.
//
// CSV output: pass,requested_ms,actual_ms,system_ms,median,mean,p95,p99,stdev,max
// system_ms is the resolution in force right after the request. If it is below
// requested_ms, another process holds a finer resolution and that row is invalid.
//
// Build (no SDK needed, csc ships with Windows):
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize+ /platform:x64 /out:TimerSweep.exe TimerSweep.cs

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading;

static class TimerSweep
{
    [DllImport("ntdll.dll")] static extern int NtSetTimerResolution(uint desired, [MarshalAs(UnmanagedType.U1)] bool set, out uint actual);
    [DllImport("ntdll.dll")] static extern int NtQueryTimerResolution(out uint min, out uint max, out uint cur);
    [DllImport("kernel32.dll")] static extern void Sleep(uint ms);
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceCounter(out long c);
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long f);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool SetProcessInformation(IntPtr h, int cls, ref PowerThrottling info, int size);

    [StructLayout(LayoutKind.Sequential)]
    struct PowerThrottling { public uint Version, ControlMask, StateMask; }

    const int Warmup = 200;
    static double qpcFreq;

    static int Main(string[] args)
    {
        Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
        if (args.Length < 2)
        {
            Console.Error.WriteLine("usage: measure <n> | list <v1,v2,...> <n> [passes] | sweep <from> <to> <step> <n>");
            return 1;
        }

        long f; QueryPerformanceFrequency(out f); qpcFreq = f;

        // Windows 11 may ignore the resolution requested by a "background" process
        // (ProcessPowerThrottling, PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION).
        // ControlMask = EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION, StateMask = 0 -> never.
        var pt = new PowerThrottling { Version = 1, ControlMask = 0x1 | 0x4, StateMask = 0 };
        SetProcessInformation(GetCurrentProcess(), 4, ref pt, Marshal.SizeOf(typeof(PowerThrottling)));

        Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.High;
        Thread.CurrentThread.Priority = ThreadPriority.Highest;

        Console.WriteLine("pass,requested_ms,actual_ms,system_ms,median,mean,p95,p99,stdev,max");
        try
        {
            switch (args[0])
            {
                case "measure":
                    Report(0, 0, 0, Measure(int.Parse(args[1])));
                    break;

                case "list":
                {
                    var values = new List<double>();
                    foreach (var s in args[1].Split(',')) values.Add(double.Parse(s, CultureInfo.InvariantCulture));
                    int n = int.Parse(args[2]);
                    int passes = args.Length > 3 ? int.Parse(args[3]) : 1;
                    for (int p = 1; p <= passes; p++)
                    {
                        var order = new List<double>(values);
                        if (p % 2 == 0) order.Reverse();
                        foreach (var v in order) RunValue(p, v, n);
                    }
                    break;
                }

                case "sweep":
                {
                    double from = double.Parse(args[1], CultureInfo.InvariantCulture);
                    double to   = double.Parse(args[2], CultureInfo.InvariantCulture);
                    double step = double.Parse(args[3], CultureInfo.InvariantCulture);
                    int n = int.Parse(args[4]);
                    for (int i = 0; from + i * step <= to + 1e-9; i++) RunValue(1, Math.Round(from + i * step, 4), n);
                    break;
                }

                default:
                    Console.Error.WriteLine("unknown mode: " + args[0]);
                    return 1;
            }
        }
        finally
        {
            uint dummy;
            NtSetTimerResolution(0, false, out dummy);   // always release our request
        }
        return 0;
    }

    static void RunValue(int pass, double ms, int n)
    {
        uint actual, dummy, min, max, cur;
        NtSetTimerResolution(0, false, out dummy);
        NtSetTimerResolution((uint)Math.Round(ms * 10000.0), true, out actual);
        NtQueryTimerResolution(out min, out max, out cur);
        Report(pass, ms, actual, Measure(n), cur);
    }

    static double[] Measure(int n)
    {
        for (int i = 0; i < Warmup; i++) Sleep(1);
        var d = new double[n];
        long a, b;
        for (int i = 0; i < n; i++)
        {
            QueryPerformanceCounter(out a);
            Sleep(1);
            QueryPerformanceCounter(out b);
            d[i] = (b - a) * 1000.0 / qpcFreq;
        }
        return d;
    }

    static void Report(int pass, double ms, uint actual, double[] d, uint sys = 0)
    {
        if (sys == 0) { uint mn, mx; NtQueryTimerResolution(out mn, out mx, out sys); }
        Array.Sort(d);
        double sum = 0; foreach (var x in d) sum += x;
        double mean = sum / d.Length;
        double sq = 0; foreach (var x in d) sq += (x - mean) * (x - mean);
        double stdev = Math.Sqrt(sq / (d.Length - 1));
        Console.WriteLine("{0},{1:F4},{2:F4},{3:F4},{4:F4},{5:F4},{6:F4},{7:F4},{8:F4},{9:F4}",
            pass, ms, actual / 10000.0, sys / 10000.0,
            Pct(d, 0.50), mean, Pct(d, 0.95), Pct(d, 0.99), stdev, d[d.Length - 1]);
    }

    static double Pct(double[] sorted, double p)
    {
        int i = (int)Math.Ceiling(p * sorted.Length) - 1;
        return sorted[Math.Max(0, Math.Min(sorted.Length - 1, i))];
    }
}
