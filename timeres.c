#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <shlobj.h>
#include <string.h>
#include <stdlib.h>

typedef NTSTATUS (WINAPI *NtQueryTimerResolution_t)(
    PULONG MinimumResolution,
    PULONG MaximumResolution,
    PULONG CurrentResolution
);

typedef NTSTATUS (WINAPI *NtSetTimerResolution_t)(
    ULONG DesiredResolution,
    BOOLEAN SetResolution,
    PULONG ActualResolution
);

#define IDC_EDIT_MS               1001
#define IDC_BTN_SET               1002
#define IDC_BTN_MAX               1003
#define IDC_BTN_DEFAULT           1004
#define IDC_CHK_APPLY_STARTUP     1005
#define IDC_CHK_TRAY              1006
#define IDC_BTN_REFRESH           1007
#define IDC_BTN_GLOBAL            1008

#define IDC_LBL_SIGNATURE         1010

#define IDC_LBL_CURRENT_VALUE     1012
#define IDC_LBL_MAXIMUM_VALUE     1014
#define IDC_LBL_MINIMUM_VALUE     1016
#define IDC_LBL_GLOBAL_VALUE      1019

#define IDC_LINE1                 1020
#define IDC_LINE2                 1021

#define IDM_TRAY_OPEN             400
#define IDM_TRAY_EXIT             401
#define WM_TRAY                   (WM_USER + 1)
#define WM_APP_MAXIMIZE           (WM_APP + 1)

#define EDIT_MAX_CHARS            7

static const char REG_KEY[]       = "SOFTWARE\\TimerResTool";
static const char REG_VAL_MS[]    = "MsValue";
static const char REG_VAL_APPLY[] = "ApplyAtStartup";
static const char REG_VAL_TRAY[]  = "TrayMin";
static const char REG_VAL_PLACEMENT[] = "WindowPlacement";
static const char TASK_NAME[]     = "TimerResTool_ApplyAtStartup";

// Without this machine-wide setting, Windows 10 2004+ services each process at the
// resolution that process itself requested, so anything we set here affects nothing
// but our own idle message loop. With it, our request applies system-wide again.
static const char GTR_KEY[] = "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";
static const char GTR_VAL[] = "GlobalTimerResolutionRequests";

static COLORREF clrBg        = RGB(32, 32, 32);
static COLORREF clrText      = RGB(220, 220, 220);
static COLORREF clrBtnText   = RGB(230, 230, 230);
static COLORREF clrBtnBg     = RGB(38, 38, 38);
static COLORREF clrBtnPressed = RGB(62, 62, 62);
static COLORREF clrBtnBorder = RGB(120, 120, 120);
static COLORREF clrSignature = RGB(90, 90, 90);
static COLORREF clrLine      = RGB(50, 50, 50);

static HBRUSH hBgBrush = NULL;
static HBRUSH hLineBrush = NULL;

static HWND hMainDlg = NULL;
static HWND hEditMs  = NULL;
static HWND hChkApplyStartup = NULL;
static HWND hChkTray = NULL;

static HWND hLblCurVal = NULL;
static HWND hLblMaxVal = NULL;
static HWND hLblMinVal = NULL;
static HWND hLblGlobal = NULL;
static LONG_PTR gOldEditProc = 0;

// Last value we asked for. 0 means we hold no request.
static double gRequestedMs = 0.0;

static NtQueryTimerResolution_t pNtQuery = NULL;
static NtSetTimerResolution_t   pNtSet   = NULL;

static NOTIFYICONDATAA nid = {0};
static BOOL trayVisible = FALSE;

// Broadcast by Explorer when the taskbar is (re)created; tray icons must be re-added.
static UINT gMsgTaskbarCreated = 0;

static double ReadConfigMs(void)
{
    double val = 0.0;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return val;
    // Stored by WriteConfigMs as REG_SZ ("%.4f"), so read into a string and convert.
    char buf[32] = {0};
    DWORD sz = sizeof(buf);
    if (RegGetValueA(hKey, NULL, REG_VAL_MS, RRF_RT_REG_SZ, NULL, buf, &sz) == ERROR_SUCCESS)
        val = atof(buf);
    RegCloseKey(hKey);
    return val;
}

static void WriteConfigMs(double ms)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;
    char buf[32];
    sprintf_s(buf, sizeof(buf), "%.4f", ms);
    RegSetValueExA(hKey, REG_VAL_MS, 0, REG_SZ, (const BYTE*)buf, (DWORD)(strlen(buf)+1));
    RegCloseKey(hKey);
}

static BOOL ReadBoolConfig(const char* name)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;
    DWORD val = 0, sz = sizeof(val);
    BOOL res = FALSE;
    if (RegQueryValueExA(hKey, name, NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
        res = (val != 0);
    RegCloseKey(hKey);
    return res;
}

static void WriteBoolConfig(const char* name, BOOL val)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;
    DWORD v = val ? 1 : 0;
    RegSetValueExA(hKey, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(hKey);
}

// Standard window placement persistence: normal rectangle plus maximized state,
// stored as a raw WINDOWPLACEMENT. Minimized (or hidden in the tray) is never
// restored; the window comes back as it was before it was minimized.
static void SaveWindowPlacement(HWND hwnd)
{
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp))
        return;

    BOOL maximized = (wp.showCmd == SW_SHOWMAXIMIZED) ||
        (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
    wp.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp.flags = 0;

    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExA(hKey, REG_VAL_PLACEMENT, 0, REG_BINARY, (const BYTE*)&wp, sizeof(wp));
    RegCloseKey(hKey);
}

static BOOL ReadWindowPlacement(WINDOWPLACEMENT* wp)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;
    DWORD sz = sizeof(*wp), type = 0;
    LONG r = RegQueryValueExA(hKey, REG_VAL_PLACEMENT, NULL, &type, (LPBYTE)wp, &sz);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS && type == REG_BINARY &&
            sz == sizeof(*wp) && wp->length == sizeof(*wp));
}

// First run: center on the primary monitor's work area (taskbar excluded), in
// physical pixels, so it is centered at any resolution and scale factor.
static void CenterOnPrimaryMonitor(HWND hwnd)
{
    POINT origin = {0, 0};
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi))
        return;

    // Twice: moving a per-monitor-DPI window can rescale it, which changes its size.
    for (int pass = 0; pass < 2; pass++)
    {
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        int y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
        if (x < mi.rcWork.left) x = mi.rcWork.left;   // larger than the screen:
        if (y < mi.rcWork.top)  y = mi.rcWork.top;    // keep the title bar reachable
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// Called from WM_INITDIALOG, while the dialog is still hidden.
static void RestoreWindowPlacement(HWND hwnd)
{
    WINDOWPLACEMENT wp;
    if (!ReadWindowPlacement(&wp))
    {
        CenterOnPrimaryMonitor(hwnd);
        return;
    }

    LONG style = GetWindowLongA(hwnd, GWL_STYLE);
    RECT* rc = &wp.rcNormalPosition;

    // A fixed-size window keeps the size its template gives it at the current DPI;
    // only a resizable one gets its saved size back.
    if (!(style & WS_THICKFRAME))
    {
        RECT cur;
        GetWindowRect(hwnd, &cur);
        rc->right  = rc->left + (cur.right - cur.left);
        rc->bottom = rc->top  + (cur.bottom - cur.top);
    }

    // The monitor it was on may be gone, or the resolution lowered: start centered.
    if (MonitorFromRect(rc, MONITOR_DEFAULTTONULL) == NULL)
    {
        CenterOnPrimaryMonitor(hwnd);
        return;
    }

    BOOL maximized = (wp.showCmd == SW_SHOWMAXIMIZED);

    // Position only; DialogBox shows the window itself once WM_INITDIALOG returns.
    wp.showCmd = SW_HIDE;
    wp.flags = 0;
    SetWindowPlacement(hwnd, &wp);

    if (maximized && (style & WS_MAXIMIZEBOX))
        PostMessageA(hwnd, WM_APP_MAXIMIZE, 0, 0);
}

static BOOL ReadGlobalMode(void)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, GTR_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;
    DWORD val = 0, sz = sizeof(val), type = 0;
    BOOL on = FALSE;
    if (RegQueryValueExA(hKey, GTR_VAL, NULL, &type, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
        on = (type == REG_DWORD && val != 0);
    RegCloseKey(hKey);
    return on;
}

static BOOL SetGlobalMode(BOOL enable)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, GTR_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;

    LONG r;
    if (enable)
    {
        DWORD v = 1;
        r = RegSetValueExA(hKey, GTR_VAL, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    }
    else
    {
        // Remove the value rather than leaving a 0 behind, so the machine goes
        // back to the stock Windows configuration.
        r = RegDeleteValueA(hKey, GTR_VAL);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }

    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS);
}

static void RefreshGlobalMode(HWND hwndDlg)
{
    BOOL on = ReadGlobalMode();

    if (hLblGlobal)
        SetWindowTextA(hLblGlobal, on ? "global mode: ON"
                                      : "global mode: OFF (per-process)");

    HWND btn = GetDlgItem(hwndDlg, IDC_BTN_GLOBAL);
    if (btn)
    {
        SetWindowTextA(btn, on ? "Disable global" : "Enable global");
        InvalidateRect(btn, NULL, TRUE);   // owner-draw needs a nudge to repaint
    }
}

static void LoadNtFunctions(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;
    pNtQuery = (NtQueryTimerResolution_t)GetProcAddress(ntdll, "NtQueryTimerResolution");
    pNtSet   = (NtSetTimerResolution_t)GetProcAddress(ntdll, "NtSetTimerResolution");
}

static BOOL GetTimerInfo(ULONG* pMin, ULONG* pMax, ULONG* pCur)
{
    if (!pNtQuery || !pMin || !pMax || !pCur) return FALSE;
    *pMin = *pMax = *pCur = 0;
    return (pNtQuery(pMin, pMax, pCur) >= 0);
}

static double UnitsToMs(ULONG units)
{
    return (double)units / 10000.0;
}

static ULONG MsToUnits(double ms)
{
    return (ULONG)(ms * 10000.0 + 0.5);
}

// Returns the resolution the system actually granted, in 100 ns units, or 0 on
// failure. This is the effective system-wide value (the finest outstanding request
// from any process), not necessarily what we asked for.
static ULONG SetTimerResolutionMs(double ms)
{
    if (!pNtSet || ms <= 0.0) return 0;
    ULONG units = MsToUnits(ms);
    ULONG actual = 0;
    if (pNtSet(units, TRUE, &actual) < 0) return 0;
    return actual;
}

static void ReleaseTimerResolution(void)
{
    if (!pNtSet) return;
    ULONG actual;
    (void)pNtSet(0, FALSE, &actual);
}

// Earlier versions dropped a .url in the Startup folder. That can never launch this
// app, which requires elevation, so clean up any leftover from an older install.
static void RemoveLegacyStartupShortcut(void)
{
    char startupPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startupPath) != S_OK)
        return;

    char urlPath[MAX_PATH + 32];
    snprintf(urlPath, sizeof(urlPath), "%s\\TimerResTool.url", startupPath);
    DeleteFileA(urlPath);
}

static BOOL RunSchTasks(const char* args)
{
    char sysDir[MAX_PATH];
    if (!GetSystemDirectoryA(sysDir, sizeof(sysDir)))
        return FALSE;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "\"%s\\schtasks.exe\" %s", sysDir, args);

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    // schtasks may prompt for a password on stdin; give it NUL so it fails fast
    // instead of hanging a process that has no console.
    HANDLE hNul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    if (hNul != INVALID_HANDLE_VALUE)
    {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = si.hStdOutput = si.hStdError = hNul;
    }

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, (hNul != INVALID_HANDLE_VALUE),
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (ok)
    {
        DWORD code = 1;
        if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &code);
        else
            TerminateProcess(pi.hProcess, 1);
        ok = (code == 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    if (hNul != INVALID_HANDLE_VALUE)
        CloseHandle(hNul);

    return ok;
}

// A Startup-folder entry cannot start an app that requires administrator, so the
// autostart is a scheduled task that runs at logon with the highest privileges.
static BOOL SyncStartupTask(BOOL enable)
{
    RemoveLegacyStartupShortcut();

    char args[1024];

    if (!enable)
    {
        // Deleting a task that was never created reports an error; not a failure here.
        snprintf(args, sizeof(args), "/Delete /F /TN \"%s\"", TASK_NAME);
        RunSchTasks(args);
        return TRUE;
    }

    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exePath, sizeof(exePath)))
        return FALSE;

    // Bind the task to the current user with an interactive token, which keeps it
    // elevated without storing a password.
    char principal[300] = {0};
    char domain[128], user[128];
    if (GetEnvironmentVariableA("USERDOMAIN", domain, sizeof(domain)) &&
        GetEnvironmentVariableA("USERNAME", user, sizeof(user)))
    {
        snprintf(principal, sizeof(principal), " /RU \"%s\\%s\" /IT", domain, user);
    }

    snprintf(args, sizeof(args),
        "/Create /F /TN \"%s\" /TR \"\\\"%s\\\"\" /SC ONLOGON /RL HIGHEST%s",
        TASK_NAME, exePath, principal);

    return RunSchTasks(args);
}

static void RefreshInfo(HWND hwndDlg)
{
    if (!hwndDlg) return;

    ULONG min, max, cur;
    char buf[80];

    RefreshGlobalMode(hwndDlg);

    if (!GetTimerInfo(&min, &max, &cur))
    {
        if (hLblCurVal) SetWindowTextA(hLblCurVal, "current: --   requested: --");
        if (hLblMaxVal) SetWindowTextA(hLblMaxVal, "maximum: --");
        if (hLblMinVal) SetWindowTextA(hLblMinVal, "minimum: --");
        return;
    }

    // "current" is what the system reports now; "requested" is what we asked for.
    // They diverge when another process holds a finer request than ours.
    if (gRequestedMs > 0.0)
        sprintf_s(buf, sizeof(buf), "current: %.4f ms   requested: %.4f ms",
                  UnitsToMs(cur), gRequestedMs);
    else
        sprintf_s(buf, sizeof(buf), "current: %.4f ms   requested: --", UnitsToMs(cur));
    if (hLblCurVal) SetWindowTextA(hLblCurVal, buf);

    sprintf_s(buf, sizeof(buf), "maximum: %.4f ms", UnitsToMs(max));
    if (hLblMaxVal) SetWindowTextA(hLblMaxVal, buf);

    sprintf_s(buf, sizeof(buf), "minimum: %.4f ms", UnitsToMs(min));
    if (hLblMinVal) SetWindowTextA(hLblMinVal, buf);
}


static void EnsureTray(HWND hwndDlg)
{
    if (trayVisible) return;
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwndDlg;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strncpy_s(nid.szTip, sizeof(nid.szTip), "Timer Resolution", _TRUNCATE);
    Shell_NotifyIconA(NIM_ADD, &nid);
    trayVisible = TRUE;
}

static void RemoveTrayIcon(void)
{
    if (!trayVisible) return;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    trayVisible = FALSE;
}

// Would replacing the current selection with `ins` still leave a valid entry
// (digits and at most one dot, EDIT_MAX_CHARS long at most)?
static BOOL EditAcceptsInsert(HWND hwnd, const char* ins)
{
    char buf[64];
    GetWindowTextA(hwnd, buf, sizeof(buf));
    DWORD len = (DWORD)strlen(buf);

    DWORD selStart = 0, selEnd = 0;
    SendMessageA(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd > len) selEnd = len;
    if (selStart > selEnd) selStart = selEnd;

    DWORD insLen = (DWORD)strlen(ins);
    if (insLen == 0 || selStart + insLen + (len - selEnd) > EDIT_MAX_CHARS)
        return FALSE;

    char result[64];
    memcpy(result, buf, selStart);
    memcpy(result + selStart, ins, insLen);
    memcpy(result + selStart + insLen, buf + selEnd, len - selEnd + 1);

    int dots = 0;
    for (const char* p = result; *p; p++)
    {
        if (*p == '.') dots++;
        else if (*p < '0' || *p > '9') return FALSE;
    }
    return dots <= 1;
}

static void EditFilteredPaste(HWND hwnd)
{
    char text[64] = {0};
    if (OpenClipboard(hwnd))
    {
        HANDLE h = GetClipboardData(CF_TEXT);
        const char* src = h ? (const char*)GlobalLock(h) : NULL;
        if (src)
        {
            strncpy_s(text, sizeof(text), src, _TRUNCATE);
            GlobalUnlock(h);
        }
        CloseClipboard();
    }

    // Tolerate surrounding whitespace, e.g. a value copied from a text file.
    char* s = text;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;

    if (EditAcceptsInsert(hwnd, s))
        SendMessageA(hwnd, EM_REPLACESEL, TRUE, (LPARAM)s);
    else
        MessageBeep(MB_OK);
}

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_CHAR)
    {
        char c = (char)wParam;
        BOOL allow = FALSE;

        if ((c >= '0' && c <= '9') || c == '.')
        {
            char ins[2] = { c, 0 };
            allow = EditAcceptsInsert(hwnd, ins);
        }
        else if (c == 8 || c == 3 || c == 22 || c == 24 || c == 26)
        {
            // Backspace, Ctrl+C, Ctrl+V (arrives as the filtered WM_PASTE below),
            // Ctrl+X, Ctrl+Z
            allow = TRUE;
        }
        else if (c == 1)
        {
            SendMessageA(hwnd, EM_SETSEL, 0, -1);   // Ctrl+A
        }
        else if (c == '\r')
        {
            // The multiline edit swallows Enter, so the dialog never sees it.
            HWND dlg = GetParent(hwnd);
            SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(IDC_BTN_SET, BN_CLICKED),
                         (LPARAM)GetDlgItem(dlg, IDC_BTN_SET));
        }

        if (allow)
            return CallWindowProcA((WNDPROC)gOldEditProc, hwnd, uMsg, wParam, lParam);

        return 0;
    }

    if (uMsg == WM_PASTE)
    {
        EditFilteredPaste(hwnd);
        return 0;
    }

    return CallWindowProcA((WNDPROC)gOldEditProc, hwnd, uMsg, wParam, lParam);
}

static void ShowMainWindow(HWND hwnd)
{
    // Hidden in the tray the window is still minimized; SW_SHOW alone would
    // bring it back as a taskbar button instead of on screen.
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
}

static LRESULT CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            hMainDlg = hwndDlg;

            hEditMs = GetDlgItem(hwndDlg, IDC_EDIT_MS);

            // Vertically center text using EM_SETRECT (multilines allowed via ES_MULTILINE in .rc)
            {
                RECT rcEdit;
                GetClientRect(hEditMs, &rcEdit);

                HDC hdcEdit = GetDC(hEditMs);
                HFONT hFont = (HFONT)SendMessage(hEditMs, WM_GETFONT, 0, 0);
                HFONT hOldFont = (HFONT)SelectObject(hdcEdit, hFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdcEdit, &tm);
                SelectObject(hdcEdit, hOldFont);
                ReleaseDC(hEditMs, hdcEdit);

                rcEdit.top = (rcEdit.bottom - tm.tmHeight) / 2;
                SendMessage(hEditMs, EM_SETRECT, 0, (LPARAM)&rcEdit);
            }

            // Default value and input filter
            SetWindowTextA(hEditMs, "0.5000");

            gOldEditProc = SetWindowLongPtrA(hEditMs, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            hChkApplyStartup = GetDlgItem(hwndDlg, IDC_CHK_APPLY_STARTUP);
            hChkTray = GetDlgItem(hwndDlg, IDC_CHK_TRAY);

            hLblCurVal = GetDlgItem(hwndDlg, IDC_LBL_CURRENT_VALUE);
            hLblMaxVal = GetDlgItem(hwndDlg, IDC_LBL_MAXIMUM_VALUE);
            hLblMinVal = GetDlgItem(hwndDlg, IDC_LBL_MINIMUM_VALUE);
            hLblGlobal = GetDlgItem(hwndDlg, IDC_LBL_GLOBAL_VALUE);

            // Restore checkboxes
            SendMessage(hChkApplyStartup, BM_SETCHECK,
                (WPARAM)(ReadBoolConfig(REG_VAL_APPLY) ? BST_CHECKED : BST_UNCHECKED), 0);
            SendMessage(hChkTray, BM_SETCHECK,
                (WPARAM)(ReadBoolConfig(REG_VAL_TRAY) ? BST_CHECKED : BST_UNCHECKED), 0);

            // Show the last saved value either way; only apply it if asked to.
            double msVal = ReadConfigMs();
            if (msVal > 0.0)
            {
                char buf[32];
                sprintf_s(buf, sizeof(buf), "%.4f", msVal);
                SetWindowTextA(hEditMs, buf);
                if (ReadBoolConfig(REG_VAL_APPLY))
                {
                    gRequestedMs  = msVal;
                    SetTimerResolutionMs(msVal);
                }
            }

            // Explorer runs at medium integrity; let its TaskbarCreated broadcast
            // through to this elevated window so the tray icon survives a restart.
            if (gMsgTaskbarCreated)
                ChangeWindowMessageFilterEx(hwndDlg, gMsgTaskbarCreated, MSGFLT_ALLOW, NULL);

            EnsureTray(hwndDlg);
            RefreshInfo(hwndDlg);
            RestoreWindowPlacement(hwndDlg);

            return TRUE;
        }

        case WM_TRAY:
        {
            if (LOWORD(lParam) == WM_RBUTTONUP)
            {
                POINT pt;
                GetCursorPos(&pt);
                HMENU menu = CreatePopupMenu();
                AppendMenuA(menu, MF_STRING, IDM_TRAY_OPEN, "Open");
                AppendMenuA(menu, MF_STRING, IDM_TRAY_EXIT, "Exit");
                SetForegroundWindow(hwndDlg);
                int cmd = (int)TrackPopupMenu(menu,
                    TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_RIGHTBUTTON,
                    pt.x, pt.y, 0, hwndDlg, NULL);
                DestroyMenu(menu);

                if (cmd == IDM_TRAY_OPEN)
                {
                    ShowMainWindow(hwndDlg);
                }
                else if (cmd == IDM_TRAY_EXIT)
                {
                    SendMessage(hwndDlg, WM_CLOSE, 0, 0);
                }
            }
            else if (LOWORD(lParam) == WM_LBUTTONUP)
            {
                ShowMainWindow(hwndDlg);
            }
            break;
        }

        case WM_APP_MAXIMIZE:
        {
            ShowWindow(hwndDlg, SW_MAXIMIZE);
            return TRUE;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);

            int id = GetDlgCtrlID((HWND)lParam);

            // Divider lines: thin dark gray
            if (id == IDC_LINE1 || id == IDC_LINE2)
            {
                if (!hLineBrush)
                    hLineBrush = CreateSolidBrush(clrLine);
                SetBkColor(hdc, clrLine);
                SetTextColor(hdc, clrLine);
                SetBkMode(hdc, OPAQUE);
                return (LRESULT)hLineBrush;
            }

            // Signature subtle color
            if (id == IDC_LBL_SIGNATURE)
            {
                SetTextColor(hdc, clrSignature);
                return (LRESULT)hBgBrush;
            }

            SetTextColor(hdc, clrText);
            return (LRESULT)hBgBrush;
        }

        case WM_CTLCOLOREDIT:
        {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, clrText);
            SetBkColor(hdc, clrBg);
            SetBkMode(hdc, OPAQUE);
            return (LRESULT)hBgBrush;
        }

        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (!dis || dis->CtlType != ODT_BUTTON) return 0;

            HDC hdc = dis->hDC;
            RECT r = dis->rcItem;

            BOOL disabled = (dis->itemState & ODS_DISABLED) != 0;

            BOOL pressed = (dis->itemState & ODS_SELECTED) != 0;

            HBRUSH bg = CreateSolidBrush(pressed ? clrBtnPressed : clrBtnBg);
            FillRect(hdc, &r, bg);
            DeleteObject(bg);

            HPEN pen = CreatePen(PS_SOLID, 1, disabled ? clrLine : clrBtnBorder);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, r.left, r.top, r.right, r.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            char txt[64];
            GetWindowTextA((HWND)dis->hwndItem, txt, _countof(txt));

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, disabled ? clrSignature : clrBtnText);
            DrawTextA(hdc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            return TRUE;
        }

        case WM_SIZE:
        {
            if (wParam == SIZE_MINIMIZED)
            {
                BOOL trayMin = (SendMessage(hChkTray, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (trayMin)
                {
                    ShowWindow(hwndDlg, SW_HIDE);
                    EnsureTray(hwndDlg);
                }
                return TRUE;
            }
            return 0;
        }

        case WM_COMMAND:
        {
            WORD wId = LOWORD(wParam);

            if (wId == IDC_BTN_SET)
            {
                char buf[64];
                GetWindowTextA(hEditMs, buf, sizeof(buf));

                // Empty (e.g. right after Default) or just "." is not a request;
                // don't silently turn it into 0.5000.
                if (strspn(buf, ".") == strlen(buf))
                {
                    MessageBeep(MB_OK);
                    SetFocus(hEditMs);
                    break;
                }

                double ms = atof(buf);

                if (ms < 0.500) ms = 0.500;
                if (ms > 15.625) ms = 15.625;

                sprintf_s(buf, sizeof(buf), "%.4f", ms);
                SetWindowTextA(hEditMs, buf);

                gRequestedMs  = ms;
                SetTimerResolutionMs(ms);
                WriteConfigMs(ms);
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_BTN_MAX)
            {
                // Max = best resolution = 0.500 ms (hardcoded lower limit)
                double ms = 0.500;
                char buf[32];
                sprintf_s(buf, sizeof(buf), "%.4f", ms);
                SetWindowTextA(hEditMs, buf);
                gRequestedMs  = ms;
                SetTimerResolutionMs(ms);
                WriteConfigMs(ms);
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_BTN_DEFAULT)
            {
                ReleaseTimerResolution();
                gRequestedMs  = 0.0;
                WriteConfigMs(0.0);
                SetWindowTextA(hEditMs, "");
                SendMessage(hChkApplyStartup, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
                WriteBoolConfig(REG_VAL_APPLY, FALSE);
                SyncStartupTask(FALSE);
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_BTN_REFRESH)
            {
                // Manual only, on demand. No timer, no thread: the app costs
                // nothing while it sits idle.
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_BTN_GLOBAL)
            {
                BOOL on = ReadGlobalMode();

                if (SetGlobalMode(!on))
                    MessageBoxA(hwndDlg, on
                        ? "Global timer mode disabled.\n\n"
                          "Restart Windows for it to take effect. Until you do,\n"
                          "requests still apply system-wide."
                        : "Global timer mode enabled.\n\n"
                          "Restart Windows for it to take effect. Until you do, timer\n"
                          "resolution requests still apply per-process only, and this\n"
                          "app has no effect on other programs.",
                        "Timer Resolution", MB_OK | MB_ICONINFORMATION);
                else
                    MessageBoxA(hwndDlg,
                        "Could not write the setting.\n"
                        "Run this app as administrator and try again.",
                        "Timer Resolution", MB_OK | MB_ICONWARNING);

                RefreshGlobalMode(hwndDlg);
                break;
            }

            if (wId == IDC_CHK_APPLY_STARTUP)
            {
                BOOL v = (BOOL)(SendMessage(hChkApplyStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (!SyncStartupTask(v))
                {
                    MessageBoxA(hwndDlg,
                        "Could not register the startup task.\n"
                        "Run this app as administrator and try again.",
                        "Timer Resolution", MB_OK | MB_ICONWARNING);
                    v = FALSE;
                    SendMessage(hChkApplyStartup, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
                }
                WriteBoolConfig(REG_VAL_APPLY, v);
                break;
            }

            if (wId == IDC_CHK_TRAY)
            {
                BOOL v = (BOOL)(SendMessage(hChkTray, BM_GETCHECK, 0, 0) == BST_CHECKED);
                WriteBoolConfig(REG_VAL_TRAY, v);
                break;
            }

            return TRUE;
        }

        case WM_CLOSE:
        {
            SaveWindowPlacement(hwndDlg);
            RemoveTrayIcon();
            EndDialog(hwndDlg, 0);
            return TRUE;
        }

        case WM_ENDSESSION:
        {
            // Logoff/shutdown ends the process without a WM_CLOSE.
            if (wParam)
                SaveWindowPlacement(hwndDlg);
            return 0;
        }

        case WM_NCDESTROY:
        {
            if (hLineBrush) { DeleteObject(hLineBrush); hLineBrush = NULL; }
            return 0;
        }

        default:
            if (gMsgTaskbarCreated && uMsg == gMsgTaskbarCreated)
            {
                trayVisible = FALSE;   // the old icon died with the old taskbar
                EnsureTray(hwndDlg);
            }
            return 0;
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Single instance
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\TimerResTool_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hExisting = FindWindowA(NULL, "New Timer Resolution");
        if (hExisting)
        {
            if (IsIconic(hExisting)) ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        }
        CloseHandle(hMutex);
        return 0;
    }

    LoadNtFunctions();
    gMsgTaskbarCreated = RegisterWindowMessageA("TaskbarCreated");

    hBgBrush = CreateSolidBrush(clrBg);
    DialogBoxParamA(hInstance, MAKEINTRESOURCEA(1), NULL, MainDlgProc, 0);
    if (hBgBrush) DeleteObject(hBgBrush);

    CloseHandle(hMutex);
    return 0;
}
