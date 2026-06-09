# Timer Resolution Tool

## Project Structure
- timeres.c - Main source code
- timeres.rc - Resource script (dialog layout)
- app.manifest - Admin required + DPI aware manifest
- push-to-github.ps1 - Script to push to GitHub

## Build
```powershell
cd D:\timeres
& "C:\msys64\msys2_shell.cmd" -defterm -no-start -ucrt64 -full-path -here -c "cd /d/timeres; x86_64-w64-mingw32-gcc -mwindows -O2 -Wall -o timeres.exe timeres.c timeres.rc -lkernel32 -luser32 -lgdi32 -lshell32"
```

## Push to GitHub

### Option 1: Use the script
```
cd D:\timeres
.\push-to-github.ps1 TU_TOKEN
```

### Option 2: Manual upload
1. Create a repository on GitHub at: https://github.com/new
2. Name it: `windows-timer-resolution-stressced-version`
3. Copy the SSH URL
4. Execute:
   ```
   git remote add origin git@github.com:carlosedt/windows-timer-resolution-stressced-version.git
   git push -u origin master
   ```

### Creating a GitHub Token
1. Go to: https://github.com/settings/tokens
2. Click "Generate new token"
3. Name: "timeres-upload"
4. Select scopes: `repo` (Full control)
5. Generate and copy the token

## Features
- View and set Windows timer resolution
- System tray icon
- Auto-start capability
- Single instance enforcement
- Dark UI theme

## Author
- carlosedt <carlosedt@gmail.com>
