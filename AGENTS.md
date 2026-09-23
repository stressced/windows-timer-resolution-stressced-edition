# Timeres - Timer Resolution Tool

## Build
```
cd D:\stresscedapps\timeres
& "C:\msys64\msys2_shell.cmd" -defterm -no-start -ucrt64 -full-path -here -c "cd /d/stresscedapps/timeres; x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -o timeres.exe timeres.c timeres.rc -lkernel32 -luser32 -lgdi32 -lshell32"
```
Build must be warning-free under -Wall.

Toolchain (checked 2026-09-23): MSYS2 is installed at C:\msys64 again (UCRT64,
gcc 16.1) and the command above builds warning-free as written. A second MinGW
gcc (15.2, with windres) lives in C:\ProgramData\mingw64\mingw64\bin and MSVC
Build Tools 2022 are also present, but the MSYS2 command is the reference build.

## Key Files
- timeres.c - Main source code
- timeres.rc - Resource script (dialog layout)
- app.manifest - Admin required + DPI aware manifest

## UI Requirements
- Dark theme: black/gray background, light text, custom-drawn dark buttons
- Edit control (IDC_EDIT_MS):
  - Same size as buttons (20px height)
  - Vertically centered text via ES_MULTILINE + EM_SETRECT
  - Only digits 0-9, one dot ".", backspace; max 7 chars. The filter accounts
    for the current selection, validates pasted text (WM_PASTE), allows
    Ctrl+A/C/X/V/Z, and Enter triggers Set.
  - Default value: 0.5000. Keep it (and Max = 0.5000) neutral: the tuned value
    below (0.5023) is specific to the owner's machine and Windows build and must
    not become the shipped default. On startup the edit shows the saved MsValue
    if there is one.
- Buttons: Set, Max, Default
- Checkboxes: Apply at startup, Minimize to system tray
- Info labels: current, maximum, minimum

## Code Structure
- Edit control subclassed (EditSubclassProc) for input filtering
- System tray icon with Open/Exit context menu. Opening from the tray must use
  SW_RESTORE (ShowMainWindow): the hidden window is still minimized. The icon is
  re-added on the "TaskbarCreated" broadcast, which has to be let through UIPI
  with ChangeWindowMessageFilterEx because the app is elevated.
- Single instance via global mutex
- Config stored in HKCU\SOFTWARE\TimerResTool
  - MsValue is REG_SZ ("%.4f"). Read it into a char buffer and atof it - never
    into a double directly, RegGetValue copies the raw string bytes.
  - WindowPlacement is REG_BINARY (a WINDOWPLACEMENT), saved on WM_CLOSE and
    WM_ENDSESSION. First run (no value) centers on the primary monitor's work
    area; if the saved rect is on no monitor any more it centers again. The
    dialog is fixed-size, so saved size and maximized state are only applied
    when the style has WS_THICKFRAME / WS_MAXIMIZEBOX. Never restored minimized.
- "Apply at startup" registers a scheduled task (SyncStartupTask), not a Startup
  folder entry: the manifest requires administrator, and Windows will not launch
  an elevated app from the Startup folder. Task runs at logon with /RL HIGHEST
  and /IT so no password is stored. Legacy .url shortcuts are cleaned up on sync.

## Timer Resolution Notes
All of the below was measured on this machine, not assumed. Numbers last
re-measured 2026-08-31 on Win11 26200 (25H2), Ryzen 7 9800X3D, QPC 10 MHz,
platform floor 0.5000 ms / ceiling 15.6250 ms.

READ THIS FIRST: the tick threshold is build-dependent and has already moved
once. On 26100 it sat at 5023 units; on 26200 it sits at 5018. Re-measure after
every feature update instead of trusting the numbers below.

- NtQueryTimerResolution naming is inverted vs intuition and the UI matches the
  API on purpose: Minimum is the coarsest interval (~15.625 ms), Maximum is the
  finest (~0.500 ms). Do not "fix" these labels.
- The system resolution is the finest value requested by any running process.
  A higher request cannot override a lower one - the arbitration is min() and the
  API has no priority or override parameter. A game sitting on the platform floor
  (0.500 ms) cannot be outbid.
- GlobalTimerResolutionRequests is what makes this app do anything at all. Without
  it, Windows 10 2004+ services each process at the resolution that process itself
  requested. Measured with Sleep(1) in a process that requests nothing, while
  another process held 0.500 ms:
      key absent  -> 15.59 ms   (no benefit whatsoever)
      key = 1     ->  1.50 ms
  The app reads this key, reports it as "global mode", and the button toggles it
  (enabling writes DWORD 1, disabling deletes the value so the machine returns to
  the stock configuration). Either direction only takes effect after a reboot, so
  the label can read ON while the running system still behaves as OFF.
- The reported CurrentResolution is NOT what a process actually gets. It read
  0.5000 ms while real Sleep(1) granularity was 15.6 ms. Never treat the reported
  number as evidence that anything improved.
- Sleep(1) rounds up to the next tick, so two ticks must exceed 1 ms. That splits
  the range into a slow side (3 ticks, median ~1.497 ms) and a fast side (2 ticks,
  median ~1.009 ms). On 26200 the boundary is between 5017 and 5018:
      0.5017 -> median 1.4972, mean 1.3877, p95 1.7155, stdev 0.2491
      0.5018 -> median 1.0090, mean 1.0248, p95 1.1013, stdev 0.0697
  Nothing below 5018 is usable. This boundary was 5023 on 26100.
- Above the boundary the fast side is NOT flat. Jitter grows as the request moves
  away from it, and past ~5030 it grows fast. Medians are 1.0090 everywhere, so
  compare mean / p95 / stdev, not the median:
      0.5018   mean 1.0248-1.0320   p95 1.10-1.16   stdev 0.070-0.086
      0.5020   mean 1.0228-1.0376   p95 1.07-1.18   stdev 0.062-0.098
      0.5023   mean 1.0249-1.0274   p95 1.10-1.13   stdev 0.069-0.077
      0.5024   mean 1.0286-1.0313   p95 1.14-1.15   stdev 0.078-0.087
      0.5030   mean 1.0322-1.0362   p95 1.18-1.25   stdev 0.087-0.095
      0.5040   mean 1.0319-1.0670   p95 1.18-1.45   stdev 0.084-0.138
      0.5050   mean 1.0423-1.0594   p95 1.28-1.45   stdev 0.107-0.137
  0.5018-0.5024 are tied within noise; 0.5050 is the worst of the fast side, with
  roughly 60% more jitter than 0.5020. This REVERSES the old 26100 note that
  called 0.5050 best - do not restore it.
- Recommended value for the owner's machine on 26200: 0.5023. It sits 5 units
  above the boundary rather than 2, which survives small future drift, and is
  still below the 5030 knee. This is a per-machine setting the owner types in,
  not the app's default (see UI Requirements).
- The ActualResolution the kernel reports back is 0.5017 for every request from
  0.5018 through 0.5050 - identical for the best and the worst value in that
  range. The reported number therefore cannot distinguish them. Only a Sleep(1)
  measurement can. Never tune by what the UI displays.

## Re-measuring
The sweep used for the numbers above was a standalone C# harness built with the
.NET Framework compiler at C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe,
which is always present (it was used while MSYS2 was missing; a C harness built
with the MSYS2 gcc works just as well now). It P/Invokes NtSetTimerResolution + NtQueryTimerResolution, times
Sleep(1) with QueryPerformanceCounter, runs at High priority, and releases the
resolution on exit. Methodology that made the results trustworthy:
- 2000-4000 samples per value, plus a 200-iteration warmup after each set.
- Run every candidate in both ascending and descending order, then interleaved
  (A,B,C,C,B,A). Single-pass sweeps produced order artifacts large enough to
  flip the ranking inside the fast side.
- Rank by mean, p95 and stdev. The median is 1.0090 for the whole fast side and
  tells you nothing.
- Because of that threshold, values must be expressible to 4 decimals. Formatting
  is "%.4f" everywhere and the edit control allows 7 chars. Do not revert to
  "%.3f": it silently rewrites 0.5024 to 0.502, which lands on the slow side of
  the threshold.
- NtSetTimerResolution returns ActualResolution (SetTimerResolutionMs returns it).

## Rules for Agents
1. No large file rewrites - use targeted patches
2. Verify critical functions exist after changes:
   - EM_SETRECT for vertical text centering
   - EditSubclassProc for input filtering
   - Tray icon functionality
   - Single instance enforcement
   - Config persistence
   - SyncStartupTask scheduled-task registration
3. Check code with git diff after modifications
4. Test compile before completing changes
5. Preserve working functionality
6. Do not enable "Apply at startup" on the owner's machine, and do not suggest it
   as a fix. It is deliberately left off (ApplyAtStartup=0) so the owner can
   launch the app by hand and A/B the machine with the timer off vs on. A system
   sitting at 15.6250 ms with the app not running is the expected idle state, not
   a misconfiguration to report.
