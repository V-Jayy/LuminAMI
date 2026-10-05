using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace LuminAMIBenchmark {
    public sealed class Measurement {
        public double WallMs, CpuMs, KernelCpuMs, UserCpuMs, SystemBusyPct;
        public double PeakWorkingSetMiB, PeakCommitMiB, ReadMiB, WriteMiB, OtherIoMiB;
        public double MaxSampleGapMs, P95SampleGapMs;
        public int Samples, ExitCode;
        public string Stdout, Stderr;
    }
    public static class Meter {
        [StructLayout(LayoutKind.Sequential)] struct FileTime { public uint Low, High; public ulong Ticks { get { return ((ulong)High << 32) | Low; } } }
        [StructLayout(LayoutKind.Sequential)] struct Io { public ulong Reads, Writes, Others, ReadBytes, WriteBytes, OtherBytes; }
        [StructLayout(LayoutKind.Sequential)] struct Memory {
            public uint Size, Faults;
            public UIntPtr PeakWorkingSet, WorkingSet, PeakPaged, Paged, PeakNonPaged, NonPaged, Commit, PeakCommit;
        }
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr h, out FileTime created, out FileTime exited, out FileTime kernel, out FileTime user);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessIoCounters(IntPtr h, out Io io);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetSystemTimes(out FileTime idle, out FileTime kernel, out FileTime user);
        [DllImport("psapi.dll", SetLastError=true)] static extern bool GetProcessMemoryInfo(IntPtr h, ref Memory memory, uint size);
        static void Require(bool ok, string operation) {
            if (!ok) throw new InvalidOperationException(operation + " failed: " + Marshal.GetLastWin32Error());
        }
        static void Gaps(Measurement result, List<double> gaps) {
            gaps.Sort(); result.Samples = gaps.Count;
            result.MaxSampleGapMs = gaps.Count == 0 ? 0 : gaps[gaps.Count - 1];
            result.P95SampleGapMs = gaps.Count == 0 ? 0 : gaps[(int)Math.Ceiling(gaps.Count * .95) - 1];
        }
        public static Measurement Idle(int milliseconds) {
            Measurement result = new Measurement(); List<double> gaps = new List<double>();
            Stopwatch clock = Stopwatch.StartNew(); double previous = 0;
            while (clock.ElapsedMilliseconds < milliseconds) {
                Thread.Sleep(10); double now = clock.Elapsed.TotalMilliseconds;
                gaps.Add(now - previous); previous = now;
            }
            result.WallMs = clock.Elapsed.TotalMilliseconds; Gaps(result, gaps); return result;
        }
        public static Measurement Run(string exe, string args, string directory) {
            FileTime idle0, kernel0, user0, idle1, kernel1, user1;
            Require(GetSystemTimes(out idle0, out kernel0, out user0), "System CPU counters");
            ProcessStartInfo start = new ProcessStartInfo(exe, args);
            start.WorkingDirectory = directory; start.UseShellExecute = false; start.CreateNoWindow = true;
            start.RedirectStandardOutput = true; start.RedirectStandardError = true;
            Measurement result = new Measurement(); List<double> gaps = new List<double>();
            Stopwatch clock = Stopwatch.StartNew();
            using (Process process = Process.Start(start)) {
                IntPtr handle = process.Handle;
                var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
                double previous = clock.Elapsed.TotalMilliseconds;
                Memory memory = new Memory(); memory.Size = (uint)Marshal.SizeOf(typeof(Memory));
                Io io = new Io(); bool memoryRead = false, ioRead = false;
                do {
                    if (GetProcessMemoryInfo(handle, ref memory, memory.Size)) {
                        memoryRead = true;
                        result.PeakWorkingSetMiB = Math.Max(result.PeakWorkingSetMiB, memory.PeakWorkingSet.ToUInt64() / 1048576.0);
                        result.PeakCommitMiB = Math.Max(result.PeakCommitMiB, memory.PeakCommit.ToUInt64() / 1048576.0);
                    }
                    if (GetProcessIoCounters(handle, out io)) ioRead = true;
                    if (process.WaitForExit(10)) break;
                    double now = clock.Elapsed.TotalMilliseconds; gaps.Add(now - previous); previous = now;
                } while (true);
                clock.Stop(); result.WallMs = clock.Elapsed.TotalMilliseconds;
                Require(memoryRead, "Process memory counters"); Require(ioRead, "Process I/O counters");
                // Keep the process handle open: cumulative CPU/I/O counters survive termination.
                Io finalIo;
                if (GetProcessIoCounters(handle, out finalIo)) io = finalIo;
                FileTime created, exited, kernel, user;
                Require(GetProcessTimes(handle, out created, out exited, out kernel, out user), "Process CPU counters");
                result.KernelCpuMs = kernel.Ticks / 10000.0; result.UserCpuMs = user.Ticks / 10000.0;
                result.CpuMs = result.KernelCpuMs + result.UserCpuMs;
                result.ReadMiB = io.ReadBytes / 1048576.0; result.WriteMiB = io.WriteBytes / 1048576.0; result.OtherIoMiB = io.OtherBytes / 1048576.0;
                result.ExitCode = process.ExitCode; result.Stdout = stdout.Result; result.Stderr = stderr.Result;
            }
            Require(GetSystemTimes(out idle1, out kernel1, out user1), "System CPU counters");
            double total = (kernel1.Ticks - kernel0.Ticks) + (user1.Ticks - user0.Ticks);
            result.SystemBusyPct = total == 0 ? 0 : 100.0 * (total - (idle1.Ticks - idle0.Ticks)) / total;
            Gaps(result, gaps); return result;
        }
    }
}
