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

#define IDI_APP                   1
#define IDI_PATREON               2
#define IDI_PATREON_HOVER         3
#define IDI_X                     4
#define IDI_X_HOVER               5
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
#define IDC_BTN_PATREON           1030
#define IDC_BTN_X                 1031

#define IDM_TRAY_OPEN             400
#define IDM_TRAY_EXIT             401
#define WM_TRAY                   (WM_USER + 1)
#define WM_APP_MAXIMIZE           (WM_APP + 1)
#define WM_APP_LAYOUT             (WM_APP + 2)

static const char URL_PATREON[] = "https://www.patreon.com/cw/Stressced";
static const char URL_X[]       = "https://x.com/Stressced";

#define EDIT_MAX_CHARS            7

// Display name: window caption (must match CAPTION in restimer.rc, FindWindow uses
// it for the single instance), tray tip and message boxes.
#define APP_NAME "ResTimer"

static const char REG_KEY[]       = "SOFTWARE\\ResTimer";
static const char REG_VAL_MS[]    = "MsValue";
static const char REG_VAL_INPUT[] = "LastInput";
static const char REG_VAL_APPLY[] = "ApplyAtStartup";
static const char REG_VAL_TRAY[]  = "TrayMin";
static const char REG_VAL_PLACEMENT[] = "WindowPlacement";
static const char TASK_NAME[]     = "ResTimer_ApplyAtStartup";

// Names used before the app became ResTimer; MigrateLegacyConfig moves them over.
static const char LEGACY_REG_KEY[]   = "SOFTWARE\\TimerResTool";
static const char LEGACY_TASK_NAME[] = "TimerResTool_ApplyAtStartup";

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

// Signature and link buttons, after Silicon Tuner's header: muted publisher text,
// --text-secondary icons that turn --accent on hover over a raised square, and a
// --series-1 ring for keyboard focus. The icon colours are baked into icons\*.ico.
static COLORREF clrPublisher = RGB(164, 164, 164);
static COLORREF clrLinkHover = RGB(42, 42, 42);
static COLORREF clrLinkEdge  = RGB(60, 60, 60);
static COLORREF clrFocusRing = RGB(64, 185, 200);

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
static LONG_PTR gOldLinkProc = 0;   // same BUTTON class proc for both link buttons

// Link icons at the size last drawn: [0] Patreon, [1] X; [i][1] is the hover colour.
static HICON gLinkIcon[2][2];
static int   gLinkIconSize = 0;

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

// Same rule as the edit filter: digits and at most one dot, EDIT_MAX_CHARS long at most.
static BOOL IsValidMsText(const char* s)
{
    if (strlen(s) > EDIT_MAX_CHARS) return FALSE;
    int dots = 0;
    for (; *s; s++)
    {
        if (*s == '.') dots++;
        else if (*s < '0' || *s > '9') return FALSE;
    }
    return dots <= 1;
}

// LastInput is the text left in the edit box, applied or not, so the next launch
// shows what was typed last. MsValue stays what was last applied (Set / Max), which
// is what "Apply at startup" uses; Default clears MsValue but not LastInput.
static BOOL ReadLastInput(char* out, DWORD outSize)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;
    char buf[32] = {0};
    DWORD sz = sizeof(buf);
    BOOL ok = (RegGetValueA(hKey, NULL, REG_VAL_INPUT, RRF_RT_REG_SZ, NULL, buf, &sz) == ERROR_SUCCESS)
              && buf[0] && IsValidMsText(buf);
    RegCloseKey(hKey);
    if (ok) strncpy_s(out, outSize, buf, _TRUNCATE);
    return ok;
}

static void SaveLastInput(void)
{
    if (!hEditMs) return;
    char buf[32];
    GetWindowTextA(hEditMs, buf, sizeof(buf));
    // An empty box (or a lone ".") keeps the previous value instead of wiping it.
    if (strspn(buf, ".") == strlen(buf) || !IsValidMsText(buf)) return;

    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExA(hKey, REG_VAL_INPUT, 0, REG_SZ, (const BYTE*)buf, (DWORD)(strlen(buf)+1));
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

// One-time move from the pre-ResTimer names: copy HKCU\SOFTWARE\TimerResTool to
// HKCU\SOFTWARE\ResTimer, delete the old key, and replace the old startup task
// (which also points at the old timeres.exe path) with one under the new name.
// Does nothing once the new key exists or when there was never an old one.
static void MigrateLegacyConfig(void)
{
    HKEY hNew, hOld;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hNew) == ERROR_SUCCESS)
    {
        RegCloseKey(hNew);
        return;
    }
    if (RegOpenKeyExA(HKEY_CURRENT_USER, LEGACY_REG_KEY, 0, KEY_READ, &hOld) != ERROR_SUCCESS)
        return;

    BOOL copied = FALSE;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_ALL_ACCESS, NULL, &hNew, NULL) == ERROR_SUCCESS)
    {
        copied = (RegCopyTreeA(hOld, NULL, hNew) == ERROR_SUCCESS);
        RegCloseKey(hNew);
    }
    RegCloseKey(hOld);
    if (!copied)
    {
        // Leave the old key alone and retry on the next launch.
        RegDeleteTreeA(HKEY_CURRENT_USER, REG_KEY);
        return;
    }
    RegDeleteTreeA(HKEY_CURRENT_USER, LEGACY_REG_KEY);

    char args[256];
    snprintf(args, sizeof(args), "/Delete /F /TN \"%s\"", LEGACY_TASK_NAME);
    RunSchTasks(args);   // fails harmlessly when there was no task
    if (ReadBoolConfig(REG_VAL_APPLY) && !SyncStartupTask(TRUE))
        WriteBoolConfig(REG_VAL_APPLY, FALSE);   // keep the checkbox honest
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


// The app icon (resource IDI_APP, built from R.png by gen_icon.ps1) at the size the
// system uses for the given metric. Shared icon: loaded once, never destroyed.
static HICON LoadAppIcon(int cxMetric, int cyMetric)
{
    HICON h = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_APP), IMAGE_ICON,
                                GetSystemMetrics(cxMetric), GetSystemMetrics(cyMetric), LR_SHARED);
    return h ? h : LoadIcon(NULL, IDI_APPLICATION);
}

static void EnsureTray(HWND hwndDlg)
{
    if (trayVisible) return;
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwndDlg;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadAppIcon(SM_CXSMICON, SM_CYSMICON);
    strncpy_s(nid.szTip, sizeof(nid.szTip), APP_NAME, _TRUNCATE);
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

// This process is elevated, and ShellExecute on the URL itself would start the
// browser elevated too. Handing the URL to explorer.exe lets the already running
// (non-elevated) shell open it in the default browser instead.
static void OpenUrlUnelevated(const char* url)
{
    char explorer[MAX_PATH];
    UINT n = GetWindowsDirectoryA(explorer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return;
    strcat_s(explorer, sizeof(explorer), "\\explorer.exe");
    ShellExecuteA(NULL, "open", explorer, url, NULL, SW_SHOWNORMAL);
}

static void FreeLinkIcons(void)
{
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            if (gLinkIcon[i][j]) { DestroyIcon(gLinkIcon[i][j]); gLinkIcon[i][j] = NULL; }
    gLinkIconSize = 0;
}

// Not LR_SHARED: that would hand back the first size loaded for every later size.
static HICON GetLinkIcon(int idx, int hover, int size)
{
    if (size != gLinkIconSize)
    {
        FreeLinkIcons();
        gLinkIconSize = size;
    }
    if (!gLinkIcon[idx][hover])
        gLinkIcon[idx][hover] = (HICON)LoadImageA(GetModuleHandleA(NULL),
            MAKEINTRESOURCEA(IDI_PATREON + idx * 2 + hover), IMAGE_ICON, size, size, 0);
    return gLinkIcon[idx][hover];
}

static BOOL IsLinkButton(int id)
{
    return id == IDC_BTN_PATREON || id == IDC_BTN_X;
}

static void DrawLinkButton(const DRAWITEMSTRUCT* dis)
{
    HDC hdc = dis->hDC;
    RECT r = dis->rcItem;
    int w = r.right - r.left, h = r.bottom - r.top;
    BOOL hover = GetWindowLongPtrA(dis->hwndItem, GWLP_USERDATA) != 0 ||
                 (dis->itemState & ODS_SELECTED);
    BOOL focus = (dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT);

    FillRect(hdc, &r, hBgBrush);
    if (hover || focus)
    {
        HBRUSH fill = CreateSolidBrush(hover ? clrLinkHover : clrBg);
        HPEN pen = CreatePen(PS_SOLID, 1, focus ? clrFocusRing : clrLinkEdge);
        HGDIOBJ oldBrush = SelectObject(hdc, fill);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        int round = (w < h ? w : h) / 3;
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, round, round);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(fill);
    }

    // Silicon Tuner: 18 px icon in a 32 x 34 button, so a bit over half the side.
    int size = (w < h ? w : h) * 9 / 16;
    if (size < 8) size = 8;
    HICON icon = GetLinkIcon(dis->CtlID == IDC_BTN_X ? 1 : 0, hover ? 1 : 0, size);
    if (icon)
        DrawIconEx(hdc, r.left + (w - size) / 2, r.top + (h - size) / 2, icon,
                   size, size, 0, NULL, DI_NORMAL);
}

// Hover state (GWLP_USERDATA) and the hand cursor for the owner-drawn link buttons.
static LRESULT CALLBACK LinkSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_MOUSEMOVE:
            if (!GetWindowLongPtrA(hwnd, GWLP_USERDATA))
            {
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 1);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
    }
    return CallWindowProcA((WNDPROC)gOldLinkProc, hwnd, uMsg, wParam, lParam);
}

// Shrinks the right-aligned signature label to its text and puts the two link
// buttons just left of it, vertically centred on it. Run again after a DPI change,
// since the text width does not scale exactly with the dialog.
static void LayoutSignature(HWND hwndDlg)
{
    HWND lbl = GetDlgItem(hwndDlg, IDC_LBL_SIGNATURE);
    HWND btnP = GetDlgItem(hwndDlg, IDC_BTN_PATREON);
    HWND btnX = GetDlgItem(hwndDlg, IDC_BTN_X);
    if (!lbl || !btnP || !btnX) return;

    char text[64];
    GetWindowTextA(lbl, text, sizeof(text));
    HDC hdc = GetDC(lbl);
    HGDIOBJ oldFont = SelectObject(hdc, (HFONT)SendMessageA(lbl, WM_GETFONT, 0, 0));
    SIZE ext = {0, 0};
    GetTextExtentPoint32A(hdc, text, (int)strlen(text), &ext);
    SelectObject(hdc, oldFont);
    ReleaseDC(lbl, hdc);

    RECT rl, rb;
    GetWindowRect(lbl, &rl);
    MapWindowPoints(NULL, hwndDlg, (POINT*)&rl, 2);
    GetWindowRect(btnX, &rb);
    int bw = rb.right - rb.left, bh = rb.bottom - rb.top;

    RECT gap = {0, 0, 3, 1};          // 3 DLU before the text, 1 DLU between buttons
    MapDialogRect(hwndDlg, &gap);

    int textLeft = rl.right - ext.cx - 2;
    SetWindowPos(lbl, NULL, textLeft, rl.top, rl.right - textLeft, rl.bottom - rl.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int y = (rl.top + rl.bottom) / 2 - bh / 2;
    int xX = textLeft - gap.right - bw;
    SetWindowPos(btnX, NULL, xX, y, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(btnP, NULL, xX - gap.bottom - bw, y, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwndDlg, NULL, TRUE);
}

static LRESULT CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            hMainDlg = hwndDlg;

            // Title bar / Alt+Tab / taskbar icon; dialogs get none by default.
            SendMessageA(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadAppIcon(SM_CXSMICON, SM_CYSMICON));
            SendMessageA(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadAppIcon(SM_CXICON, SM_CYICON));

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

            gOldLinkProc = SetWindowLongPtrA(GetDlgItem(hwndDlg, IDC_BTN_PATREON),
                                             GWLP_WNDPROC, (LONG_PTR)LinkSubclassProc);
            SetWindowLongPtrA(GetDlgItem(hwndDlg, IDC_BTN_X), GWLP_WNDPROC, (LONG_PTR)LinkSubclassProc);
            LayoutSignature(hwndDlg);

            // Restore checkboxes
            SendMessage(hChkApplyStartup, BM_SETCHECK,
                (WPARAM)(ReadBoolConfig(REG_VAL_APPLY) ? BST_CHECKED : BST_UNCHECKED), 0);
            SendMessage(hChkTray, BM_SETCHECK,
                (WPARAM)(ReadBoolConfig(REG_VAL_TRAY) ? BST_CHECKED : BST_UNCHECKED), 0);

            // The box shows what was typed last (LastInput), else the last applied
            // value, else the 0.5000 default set above. Only MsValue is ever applied.
            double msVal = ReadConfigMs();
            char lastInput[32];
            if (ReadLastInput(lastInput, sizeof(lastInput)))
                SetWindowTextA(hEditMs, lastInput);
            else if (msVal > 0.0)
            {
                char buf[32];
                sprintf_s(buf, sizeof(buf), "%.4f", msVal);
                SetWindowTextA(hEditMs, buf);
            }
            if (msVal > 0.0)
            {
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

        case WM_DPICHANGED:
        {
            // The dialog manager rescales controls and fonts after we return.
            PostMessageA(hwndDlg, WM_APP_LAYOUT, 0, 0);
            return FALSE;
        }

        case WM_APP_LAYOUT:
        {
            LayoutSignature(hwndDlg);
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

            // Signature: muted, like Silicon Tuner's "Stressced Software"
            if (id == IDC_LBL_SIGNATURE)
            {
                SetTextColor(hdc, clrPublisher);
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

            if (IsLinkButton((int)dis->CtlID))
            {
                DrawLinkButton(dis);
                return TRUE;
            }

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
                SaveLastInput();
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
                SaveLastInput();
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_BTN_DEFAULT)
            {
                // Releases the request but leaves the typed value in the box (and in
                // LastInput), so Default -> Set is a quick A/B of the same value.
                ReleaseTimerResolution();
                gRequestedMs  = 0.0;
                WriteConfigMs(0.0);
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
                        APP_NAME, MB_OK | MB_ICONINFORMATION);
                else
                    MessageBoxA(hwndDlg,
                        "Could not write the setting.\n"
                        "Run this app as administrator and try again.",
                        APP_NAME, MB_OK | MB_ICONWARNING);

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
                        APP_NAME, MB_OK | MB_ICONWARNING);
                    v = FALSE;
                    SendMessage(hChkApplyStartup, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
                }
                WriteBoolConfig(REG_VAL_APPLY, v);
                break;
            }

            if (wId == IDC_BTN_PATREON || wId == IDC_BTN_X)
            {
                OpenUrlUnelevated(wId == IDC_BTN_X ? URL_X : URL_PATREON);
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
            SaveLastInput();
            RemoveTrayIcon();
            EndDialog(hwndDlg, 0);
            return TRUE;
        }

        case WM_ENDSESSION:
        {
            // Logoff/shutdown ends the process without a WM_CLOSE.
            if (wParam)
            {
                SaveWindowPlacement(hwndDlg);
                SaveLastInput();
            }
            return 0;
        }

        case WM_NCDESTROY:
        {
            if (hLineBrush) { DeleteObject(hLineBrush); hLineBrush = NULL; }
            FreeLinkIcons();
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
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\ResTimer_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hExisting = FindWindowA(NULL, APP_NAME);
        if (hExisting)
        {
            if (IsIconic(hExisting)) ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        }
        CloseHandle(hMutex);
        return 0;
    }

    MigrateLegacyConfig();
    LoadNtFunctions();
    gMsgTaskbarCreated = RegisterWindowMessageA("TaskbarCreated");

    hBgBrush = CreateSolidBrush(clrBg);
    DialogBoxParamA(hInstance, MAKEINTRESOURCEA(1), NULL, MainDlgProc, 0);
    if (hBgBrush) DeleteObject(hBgBrush);

    CloseHandle(hMutex);
    return 0;
}
