# ResTimer - Timer Resolution Tool (repo folder: timeres)

The app is ResTimer by Stressced Software (renamed 2026-11-02 from "Timer
Resolution" / TimerResTool; the repo folder and GitHub repo keep their old names).
The name says what the app does: it sets the resolution the user types; it does
not search or tune for the best value. APP_NAME in restimer.c must equal CAPTION
in restimer.rc (FindWindow uses it for the single instance). Everything is named
ResTimer: ResTimer.exe, HKCU\SOFTWARE\ResTimer, task ResTimer_ApplyAtStartup,
mutex Global\ResTimer_SingleInstance. MigrateLegacyConfig moves the old
HKCU\SOFTWARE\TimerResTool key over on first launch and replaces the old task
TimerResTool_ApplyAtStartup; keep that code and the LEGACY_* names.

## Build
```
cd D:\stresscedapps\timeres
& "C:\msys64\msys2_shell.cmd" -defterm -no-start -ucrt64 -full-path -here -c "cd /d/stresscedapps/timeres; x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -static -o ResTimer.exe restimer.c restimer.rc -lkernel32 -luser32 -lgdi32 -lshell32"
```
Build must be warning-free under -Wall.

Equivalent build from plain PowerShell with the MinGW-Builds gcc on PATH
(C:\ProgramData\mingw64\mingw64\bin, gcc 15.2). That gcc does not compile .rc
files itself, so run windres first:
```
windres -O coff -i restimer.rc -o $env:TEMP\restimer_res.o
x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -static -o ResTimer.exe restimer.c $env:TEMP\restimer_res.o -lkernel32 -luser32 -lgdi32 -lshell32
```

Toolchain (checked 2026-11-02): MSYS2 at C:\msys64, MinGW-Builds gcc in
C:\ProgramData\mingw64, MSVC Build Tools 2022 and git are all installed again.
(On 2026-10-03, right after a clean reinstall, none of them were.)

Portability: ResTimer.exe is a single self-contained file. Keep -static and check
`objdump -p ResTimer.exe | Select-String "DLL Name"` after a build: only Windows
DLLs (KERNEL32, USER32, GDI32, SHELL32, ADVAPI32) and the api-ms-win-crt-* UCRT
forwarders (built into Windows 10+) are allowed. A libgcc / libwinpthread /
libstdc++ import would break copying the exe to another PC. Settings live in
HKCU and are created on first use, so nothing has to be copied with the exe.

Icon: restimer.ico is generated from R.png by gen_icon.ps1 (run it with
`powershell -ExecutionPolicy Bypass -File gen_icon.ps1`; it also writes the
256 px preview restimer.png). It matches TinyFanControl's T and Silicon Tuner's S:
glyph only, transparent background, no outline, gradient #9661ff -> #6632e8 ->
#4423ad, three #18122e speed lines. The .rc embeds it as IDI_APP (the first icon
group, so Explorer uses it); LoadAppIcon puts it on the window and tray icon.

## Key Files
- restimer.c - Main source code
- restimer.rc - Resource script (dialog layout, icons, version info)
- app.manifest - Admin required + DPI aware manifest
- R.png - source artwork of the icon; gen_icon.ps1 -> restimer.ico (+ restimer.png)
  and the link icons icons\patreon*.ico, icons\x*.ico
- tools\TimerSweep.cs - Sleep(1) measurement harness (see Re-measuring)
- tools\measurements\ - raw CSV results of the sweeps, named by Windows build

## UI Requirements
- Dark theme: black/gray background, light text, custom-drawn dark buttons
- Edit control (IDC_EDIT_MS):
  - Same size as buttons (20px height)
  - Vertically centered text via ES_MULTILINE + EM_SETRECT
  - Only digits 0-9, one dot ".", backspace; max 7 chars. The filter accounts
    for the current selection, validates pasted text (WM_PASTE), allows
    Ctrl+A/C/X/V/Z, and Enter triggers Set.
  - Default value: 0.5000. Keep it (and Max = 0.5000) neutral: the tuned value
    below (0.5020 on 26300) is specific to the owner's machine and Windows build
    and must not become the shipped default. On startup the edit shows LastInput
    (whatever was typed last, applied or not), else the saved MsValue, else 0.5000.
- Buttons: Set, Max, Default. Default releases the request and clears MsValue
  but leaves the typed value in the box, so Default -> Set is a quick A/B.
- Checkboxes: Apply at startup, Minimize to system tray
- Info labels: current, maximum, minimum
- Signature, bottom right: Patreon and X icon buttons, then "Stressced Software",
  styled after Silicon Tuner's header (icons #c4c4c4, #fcee0a on hover over a
  raised square, #40b9c8 keyboard-focus ring, muted #a4a4a4 text). The icon
  colours are baked into icons\*.ico by gen_icon.ps1 (Simple Icons paths, CC0).
  LayoutSignature sizes the label to its text and puts the buttons left of it,
  again after WM_DPICHANGED. Links open through explorer.exe (OpenUrlUnelevated)
  so the browser does not inherit this process's elevation; URLs are the same as
  Silicon Tuner's (https://www.patreon.com/cw/Stressced, https://x.com/Stressced).

## Code Structure
- Edit control subclassed (EditSubclassProc) for input filtering
- System tray icon with Open/Exit context menu. Opening from the tray must use
  SW_RESTORE (ShowMainWindow): the hidden window is still minimized. The icon is
  re-added on the "TaskbarCreated" broadcast, which has to be let through UIPI
  with ChangeWindowMessageFilterEx because the app is elevated.
- Single instance via global mutex
- Config stored in HKCU\SOFTWARE\ResTimer
  - MsValue is REG_SZ ("%.4f"). Read it into a char buffer and atof it - never
    into a double directly, RegGetValue copies the raw string bytes.
    MsValue is the last APPLIED value (Set / Max) and the only one "Apply at
    startup" uses.
  - LastInput is REG_SZ, the raw edit text, saved on Set / Max, WM_CLOSE and
    WM_ENDSESSION. An empty box does not overwrite it, and it is re-validated
    with the edit filter's rules when read. It is display-only, never applied.
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
re-measured 2026-10-03 on Win11 26300.9457 (26H2, clean reinstall), Ryzen 7
9800X3D, QPC 10 MHz, platform floor 0.5000 ms / ceiling 15.6250 ms. The 26200
figures (2026-08-31) are kept next to them for comparison.

READ THIS FIRST: the tick threshold and the usable window are build-dependent.
On 26100 the threshold sat at 5023 units; on 26200 and 26300 it sits at 5018.
On 26300 the upper end of the usable window dropped from ~5030 to 5023.
Re-measure after every feature update instead of trusting the numbers below.

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
  Re-checked on 26300 with key = 1, from a process that requests nothing:
      nothing held     -> median 16.04 ms
      0.5020 held      -> median 1.0095 ms, stdev 0.09
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
  Same boundary on 26300 (averages of 6 passes x 4000 samples):
      0.5017 -> mean 1.3608, p95 1.9189, stdev 0.2733
      0.5018 -> mean 1.0266, p95 1.1227, stdev 0.0734
  Nothing below 5018 is usable. This boundary was 5023 on 26100. On 26300 the
  slow side is worse than on 26200 (p95 ~1.88-1.92 for every value from 0.5000
  to 0.5017), and that includes Max = 0.5000: median 1.43, stdev 0.28.
- Above the boundary the fast side is NOT flat. Jitter grows as the request moves
  away from it, and where it starts growing depends on the build (~5030 on 26200,
  5024 on 26300). Medians are ~1.009 everywhere, so compare mean / p95 / stdev,
  not the median.
  26300, 6 alternating passes x 4000 samples (min-max across passes):
      0.5018   mean 1.0257-1.0286   p95 1.11-1.15   stdev 0.069-0.079
      0.5019   mean 1.0243-1.0266   p95 1.09-1.12   stdev 0.067-0.076
      0.5020   mean 1.0240-1.0288   p95 1.09-1.15   stdev 0.066-0.079
      0.5021   mean 1.0242-1.0279   p95 1.10-1.13   stdev 0.066-0.075
      0.5022   mean 1.0258-1.0277   p95 1.11-1.13   stdev 0.071-0.077
      0.5023   mean 1.0244-1.0278   p95 1.09-1.12   stdev 0.067-0.080
      0.5024   mean 1.0479-1.0551   p95 1.36-1.42   stdev 0.114-0.124
      0.5025   mean 1.0855-1.0982   p95 1.49-1.51   stdev 0.159-0.172
  0.5018-0.5023 are tied within noise. 0.5024 is worse in all 6 passes (stdev
  about +65%, p95 +0.29 ms) and 0.5025 is worse again. The single-pass coarse
  sweep puts 0.5025-0.5033 at stdev 0.16-0.17, jitter climbing again from 5034,
  and most of 0.5041-0.5059 back at slow-side p95 (~1.92-1.99).
  26200, for comparison (min-max across passes):
      0.5018   mean 1.0248-1.0320   p95 1.10-1.16   stdev 0.070-0.086
      0.5020   mean 1.0228-1.0376   p95 1.07-1.18   stdev 0.062-0.098
      0.5023   mean 1.0249-1.0274   p95 1.10-1.13   stdev 0.069-0.077
      0.5024   mean 1.0286-1.0313   p95 1.14-1.15   stdev 0.078-0.087
      0.5030   mean 1.0322-1.0362   p95 1.18-1.25   stdev 0.087-0.095
      0.5040   mean 1.0319-1.0670   p95 1.18-1.45   stdev 0.084-0.138
      0.5050   mean 1.0423-1.0594   p95 1.28-1.45   stdev 0.107-0.137
  On 26200, 0.5018-0.5024 were tied within noise; 0.5050 is the worst of the fast
  side, with roughly 60% more jitter than 0.5020. This REVERSES the old 26100
  note that called 0.5050 best - do not restore it.
- Recommended value for the owner's machine on 26300: 0.5020. The usable window
  is 5018-5023, and the edge that moved between 26200 and 26300 was the upper one
  (~5030 -> 5023), so 0.5020 keeps 2 units of margin below and 3 above. The
  previous pick, 0.5023, still measures fine on 26300 but is now the last good
  value, one unit from 0.5024. (On 26200 the pick was 0.5023: 5 units above the
  boundary and below the 5030 knee.) This is a per-machine setting the owner
  types in, not the app's default (see UI Requirements).
- The ActualResolution the kernel reports back is 0.5017 for every request from
  0.5018 through 0.5050 - identical for the best and the worst value in that
  range. The reported number therefore cannot distinguish them. Only a Sleep(1)
  measurement can. Never tune by what the UI displays.
  On 26300 it is not even stable: the same request usually comes back as 0.5017
  (0.4966 for 0.5002-0.5017), but 4 of the 54 fine-sweep requests between 0.5018
  and 0.5022 came back as 1.0035 while their Sleep(1) timings were normal.

## Re-measuring
The harness is tools\TimerSweep.cs (added 2026-10-03; earlier sweeps used an
uncommitted equivalent). Build it with the .NET Framework compiler at
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe, which is always present;
the exact command is in the file header, and the .exe is git-ignored. It
P/Invokes NtSetTimerResolution + NtQueryTimerResolution, times Sleep(1) with
QueryPerformanceCounter, runs at High priority, opts out of Windows 11 power
throttling (so its own request is never ignored), and releases the resolution
on exit. Raw results are in tools\measurements\.
- Close ResTimer.exe before sweeping (tray > Exit; not Default, which also wipes
  the saved value). The finest request wins, so while it holds 0.502x every
  coarser value silently runs at 0.502x. A row whose system_ms is below its
  requested_ms was contaminated this way and must be discarded.
- The runs behind the 26300 numbers (machine idle, about 3 + 5 minutes):
      TimerSweep sweep 0.5000 0.5060 0.0001 2000
      TimerSweep list 0.5017,0.5018,0.5019,0.5020,0.5021,0.5022,0.5023,0.5024,0.5025 4000 6
  To check global mode, keep ResTimer open on the pick and run
  `TimerSweep measure 3000`: that process requests nothing, like a game that
  never calls timeBeginPeriod.
Methodology that made the results trustworthy:
- 2000-4000 samples per value, plus a 200-iteration warmup after each set.
- Run every candidate in both ascending and descending order, then interleaved
  (A,B,C,C,B,A). Single-pass sweeps produced order artifacts large enough to
  flip the ranking inside the fast side.
- Rank by mean, p95 and stdev. The median is 1.0090 for the whole fast side and
  tells you nothing.
- Because of that threshold, values must be expressible to 4 decimals. Formatting
  is "%.4f" everywhere and the edit control allows 7 chars. Do not revert to
  "%.3f": it silently rewrites 0.5024 to 0.502, which was on the slow side of the
  26100 threshold (5023). With only 0.501 / 0.502 / 0.503 to choose from, whether
  any of them lands in the usable window is luck: 0.502 does on 26300 (window
  5018-5023), it did not on 26100.
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
