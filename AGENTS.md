# Timeres - Timer Resolution Tool

## Build
```
cd D:\timeres
& "C:\msys64\msys2_shell.cmd" -defterm -no-start -ucrt64 -full-path -here -c "cd /d/timeres; x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -o timeres.exe timeres.c timeres.rc -lkernel32 -luser32 -lgdi32 -lshell32"
```

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

## Rules for Agents
1. No large file rewrites - use targeted patches
2. Verify critical functions exist after changes:
   - EM_SETRECT for vertical text centering
   - EditSubclassProc for input filtering
   - Tray icon functionality
   - Single instance enforcement
   - Config persistence
3. Check code with git diff after modifications
4. Test compile before completing changes
5. Preserve working functionality
