# Timeres - Timer Resolution Tool

## Build
```
cd D:\stresscedapps\timeres
& "C:\msys64\msys2_shell.cmd" -defterm -no-start -ucrt64 -full-path -here -c "cd /d/stresscedapps/timeres; x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -o timeres.exe timeres.c timeres.rc -lkernel32 -luser32 -lgdi32 -lshell32"
```
Build must be warning-free under -Wall.

## Key Files
- timeres.c - Main source code
- timeres.rc - Resource script (dialog layout)
- app.manifest - Admin required + DPI aware manifest

## UI Requirements
- Dark theme: black/gray background, light text, custom-drawn dark buttons
- Edit control (IDC_EDIT_MS):
  - Same size as buttons (20px height)
  - Vertically centered text via ES_MULTILINE + EM_SETRECT
  - Only digits 0-9, one dot ".", backspace; max 6 chars
  - Default value: 0.500
- Buttons: Set, Max, Default
- Checkboxes: Apply at startup, Minimize to system tray
- Info labels: current, maximum, minimum

## Code Structure
- Edit control subclassed (EditSubclassProc) for input filtering
- System tray icon with Open/Exit context menu
- Single instance via global mutex
- Config stored in HKCU\SOFTWARE\TimerResTool
  - MsValue is REG_SZ ("%.3f"). Read it into a char buffer and atof it - never
    into a double directly, RegGetValue copies the raw string bytes.
- "Apply at startup" registers a scheduled task (SyncStartupTask), not a Startup
  folder entry: the manifest requires administrator, and Windows will not launch
  an elevated app from the Startup folder. Task runs at logon with /RL HIGHEST
  and /IT so no password is stored. Legacy .url shortcuts are cleaned up on sync.

## Timer Resolution Notes
All of the below was measured on this machine (Win11 26100), not assumed.

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
- Sleep(1) rounds up to the next tick, so two ticks must exceed 1 ms. Measured
  medians: 0.5000 -> 1.500 ms, 0.5020 -> 1.506 ms (3 ticks), 0.5023 and above
  -> 1.005 ms (2 ticks). The threshold is exactly 5023 units. Values just past it
  still show a p95 of ~1.507; 0.5050 was the best overall (mean 1.025, p95 1.038).
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
