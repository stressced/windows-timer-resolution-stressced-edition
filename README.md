# Windows Timer Resolution – Stressced Edition

A small, native Windows tool that raises the system timer resolution (for example
from the stock 15.625 ms down to 0.5 ms), with an honest view of what Windows is
actually doing. It's a single ~180 KB executable with no installer, no runtime and
no background threads.

The app has a dark UI and a tray icon. It remembers its settings and restores its
window position between runs.

## Features

- **Set**: sets any resolution between 0.5000 and 15.6250 ms, with 4-decimal
  precision.
- **Max**: requests the finest resolution (0.5000 ms).
- **Default**: releases the request, so Windows goes back to its normal timer.
- **Global mode toggle**: enables or disables `GlobalTimerResolutionRequests`,
  which Windows 10 2004+ needs before a timer request can affect other programs
  (see below).
- **Apply at startup**: registers a scheduled task that starts the app elevated
  at logon and re-applies your value. No password is stored.
- **Minimize to system tray**
- **Refresh**: re-reads the current, maximum and minimum resolution on demand.
  There's no polling, so the app costs nothing while idle.
- **Single instance**: launching the app a second time brings back the window
  that's already open.
- **Window position**: the first launch centers the window on the primary
  monitor. After that, the window reopens where you left it. If that monitor is
  gone, it opens centered again.

## Requirements

- Windows 10 or 11, 64-bit.
- Administrator rights. The app asks for them through its manifest, because it
  writes a machine-wide registry value and registers an elevated scheduled task.

## Usage

1. Run `timeres.exe` and accept the UAC prompt.
2. Check the **global mode** line. On Windows 10 2004 or later, if it says
   `OFF (per-process)`, click **Enable global** and **restart Windows**. Without
   this step the timer only changes inside this app's own process and has no
   effect on games or other software.
3. Type a value and press **Set** (or Enter), or press **Max**.
4. Keep the app running. The request only lasts as long as the process does:
   closing the app releases it.

## Things worth knowing

These are easy to get wrong, and most timer-resolution tools don't mention them.

- **The global setting only takes effect after a reboot.** Enabling or disabling
  global mode changes the registry right away, but Windows only reads the value
  at boot. The label can say ON while the running system still behaves as OFF.
- **The lowest request wins.** Windows applies the finest resolution that any
  running process asks for. You can't make the timer coarser than what another
  program (a game, a browser, another timer tool) is already holding.
- **The reported "current" value isn't proof that anything changed.** Windows can
  report 0.5 ms while a normal program still sleeps in 15.6 ms steps (for
  example, when global mode is off). The only reliable check is to measure real
  `Sleep(1)` timing.
- **0.5000 isn't always the best value.** `Sleep(1)` rounds up to whole timer
  ticks. A resolution just below a platform-specific threshold needs 3 ticks
  (about 1.5 ms per sleep), and one just above it needs 2 ticks (about 1.0 ms).
  On some machines a value slightly above 0.5 ms, such as 0.502x, gives
  noticeably better sleep precision than 0.5000. The exact threshold depends on
  your hardware and your Windows build, and it can move after a feature update.
  That's why the app defaults to a neutral 0.5000 instead of a tuned number. If
  you care about the last few microseconds, measure on your own machine: time
  thousands of `Sleep(1)` calls with `QueryPerformanceCounter` at a few
  candidate values, and compare the mean, p95 and standard deviation (the median
  barely changes).
- The *minimum* and *maximum* labels follow the naming of the Windows API
  (`NtQueryTimerResolution`): *maximum* is the finest interval (about 0.5 ms), and
  *minimum* is the coarsest (about 15.6 ms).

## Where settings are stored

| What | Where |
|------|-------|
| App settings (value, checkboxes, window position) | `HKCU\SOFTWARE\TimerResTool` |
| Global mode | `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel`, value `GlobalTimerResolutionRequests` |
| Autostart | Scheduled task `TimerResTool_ApplyAtStartup` |

### Uninstalling

1. In the app, press **Default** and uncheck **Apply at startup**. This removes
   the scheduled task.
2. If you enabled global mode and want the stock Windows behavior back, click
   **Disable global** and reboot.
3. Delete `timeres.exe`. Optionally, delete the `HKCU\SOFTWARE\TimerResTool` key.

## Building from source

This project uses [MSYS2](https://www.msys2.org/) (UCRT64) with the MinGW-w64
toolchain. From an MSYS2 UCRT64 shell in the project folder, run:

```sh
x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -o timeres.exe timeres.c timeres.rc \
    -lkernel32 -luser32 -lgdi32 -lshell32
```

The build is expected to produce no warnings under `-Wall`.

| File | Purpose |
|------|---------|
| `timeres.c` | The whole application |
| `timeres.rc` | Dialog layout and version info |
| `app.manifest` | Requests administrator rights and per-monitor DPI awareness |

## License

Public domain.

---

By **Stressced**.
