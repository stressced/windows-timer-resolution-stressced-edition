#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <shlobj.h>
#include <string.h>

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

#define IDC_LBL_SIGNATURE         1010

#define IDC_LBL_CURRENT_VALUE     1012
#define IDC_LBL_MAXIMUM_VALUE     1014
#define IDC_LBL_MINIMUM_VALUE     1016

#define IDC_LINE1                 1020
#define IDC_LINE2                 1021

#define IDM_TRAY_OPEN             400
#define IDM_TRAY_EXIT             401
#define WM_TRAY                   (WM_USER + 1)

static const char REG_KEY[]       = "SOFTWARE\\TimerResTool";
static const char REG_VAL_MS[]    = "MsValue";
static const char REG_VAL_APPLY[] = "ApplyAtStartup";
static const char REG_VAL_TRAY[]  = "TrayMin";

static COLORREF clrBg        = RGB(32, 32, 32);
static COLORREF clrText      = RGB(220, 220, 220);
static COLORREF clrBtnText   = RGB(230, 230, 230);
static COLORREF clrBtnBg     = RGB(38, 38, 38);
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
static LONG_PTR gOldEditProc = 0;

static NtQueryTimerResolution_t pNtQuery = NULL;
static NtSetTimerResolution_t   pNtSet   = NULL;

static NOTIFYICONDATAA nid = {0};
static BOOL trayVisible = FALSE;

static double ReadConfigMs(void)
{
    double val = 0.0;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return val;
    DWORD sz = sizeof(val);
    (void)RegGetValueA(hKey, NULL, REG_VAL_MS, RRF_RT_REG_SZ, NULL, &val, &sz);
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
    sprintf_s(buf, sizeof(buf), "%.3f", ms);
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

static void SetTimerResolutionMs(double ms)
{
    if (!pNtSet || ms <= 0.0) return;
    ULONG units = MsToUnits(ms);
    ULONG actual;
    (void)pNtSet(units, TRUE, &actual);
}

static void ReleaseTimerResolution(void)
{
    if (!pNtSet) return;
    ULONG actual;
    (void)pNtSet(0, FALSE, &actual);
}

static void SyncStartupShortcut(BOOL enable)
{
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exePath, sizeof(exePath)))
        return;

    char startupPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP | CSIDL_FLAG_CREATE, NULL, 0, startupPath) != S_OK)
        return;

    char urlPath[MAX_PATH];
    snprintf(urlPath, sizeof(urlPath), "%s\\TimerResTool.url", startupPath);

    char urlContent[1024];
    snprintf(urlContent, sizeof(urlContent),
        "[InternetShortcut]\r\nIconFile=%s\r\nIconIndex=0\r\nURL=%s\r\n",
        exePath, exePath);

    if (enable)
    {
        FILE* f = fopen(urlPath, "w");
        if (f)
        {
            fwrite(urlContent, 1, strlen(urlContent), f);
            fclose(f);
        }
    }
    else
    {
        DeleteFileA(urlPath);
    }
}

static void RefreshInfo(HWND hwndDlg)
{
    if (!hwndDlg) return;

    ULONG min, max, cur;
    char buf[64];

    if (!GetTimerInfo(&min, &max, &cur))
    {
        if (hLblCurVal) SetWindowTextA(hLblCurVal, "current: --");
        if (hLblMaxVal) SetWindowTextA(hLblMaxVal, "maximum: --");
        if (hLblMinVal) SetWindowTextA(hLblMinVal, "minimum: --");
        return;
    }

    sprintf_s(buf, sizeof(buf), "current: %.3f ms", UnitsToMs(cur));
    if (hLblCurVal) SetWindowTextA(hLblCurVal, buf);

    sprintf_s(buf, sizeof(buf), "maximum: %.3f ms", UnitsToMs(max));
    if (hLblMaxVal) SetWindowTextA(hLblMaxVal, buf);

    sprintf_s(buf, sizeof(buf), "minimum: %.3f ms", UnitsToMs(min));
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

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_CHAR)
    {
        char c = (char)wParam;
        char buf[64];
        GetWindowTextA(hwnd, buf, sizeof(buf));
        int len = (int)strlen(buf);
        BOOL allow = FALSE;

        if (c >= '0' && c <= '9')
        {
            if (len < 6) allow = TRUE;
        }
        else if (c == '.')
        {
            if (len < 6 && !strstr(buf, "."))
                allow = TRUE;
        }
        else if (c == 8)
        {
            allow = TRUE;
        }

        if (allow)
            return CallWindowProcA((WNDPROC)gOldEditProc, hwnd, uMsg, wParam, lParam);

        return 0;
    }

    return CallWindowProcA((WNDPROC)gOldEditProc, hwnd, uMsg, wParam, lParam);
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
            SetWindowTextA(hEditMs, "0.500");

            gOldEditProc = SetWindowLongPtrA(hEditMs, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            hChkApplyStartup = GetDlgItem(hwndDlg, IDC_CHK_APPLY_STARTUP);
            hChkTray = GetDlgItem(hwndDlg, IDC_CHK_TRAY);

            hLblCurVal = GetDlgItem(hwndDlg, IDC_LBL_CURRENT_VALUE);
            hLblMaxVal = GetDlgItem(hwndDlg, IDC_LBL_MAXIMUM_VALUE);
            hLblMinVal = GetDlgItem(hwndDlg, IDC_LBL_MINIMUM_VALUE);

            // Restore checkboxes
            SendMessage(hChkApplyStartup, BM_SETCHECK,
                (WPARAM)(ReadBoolConfig(REG_VAL_APPLY) ? BST_CHECKED : BST_UNCHECKED), 0);
            SendMessage(hChkTray, BM_SETCHECK,
                (WPARAM)(ReadBoolConfig(REG_VAL_TRAY) ? BST_CHECKED : BST_UNCHECKED), 0);

            // Startup apply
            double msVal = ReadConfigMs();
            if (msVal > 0.0 && ReadBoolConfig(REG_VAL_APPLY))
            {
                char buf[32];
                sprintf_s(buf, sizeof(buf), "%.3f", msVal);
                SetWindowTextA(hEditMs, buf);
                SetTimerResolutionMs(msVal);
            }

            EnsureTray(hwndDlg);
            RefreshInfo(hwndDlg);

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
                    ShowWindow(hwndDlg, SW_SHOW);
                    SetForegroundWindow(hwndDlg);
                }
                else if (cmd == IDM_TRAY_EXIT)
                {
                    SendMessage(hwndDlg, WM_CLOSE, 0, 0);
                }
            }
            else if (LOWORD(lParam) == WM_LBUTTONUP)
            {
                ShowWindow(hwndDlg, SW_SHOW);
                SetForegroundWindow(hwndDlg);
            }
            break;
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

            HBRUSH bg = CreateSolidBrush(clrBtnBg);
            FillRect(hdc, &r, bg);
            DeleteObject(bg);

            HPEN pen = CreatePen(PS_SOLID, 1, clrBtnBorder);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, r.left, r.top, r.right, r.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            char txt[64];
            GetWindowTextA((HWND)dis->hwndItem, txt, _countof(txt));

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, clrBtnText);
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
                double ms = atof(buf);

                if (ms < 0.500) ms = 0.500;
                if (ms > 15.625) ms = 15.625;

                sprintf_s(buf, sizeof(buf), "%.3f", ms);
                SetWindowTextA(hEditMs, buf);

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
                sprintf_s(buf, sizeof(buf), "%.3f", ms);
                SetWindowTextA(hEditMs, buf);
                SetTimerResolutionMs(ms);
                WriteConfigMs(ms);
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_BTN_DEFAULT)
            {
                ReleaseTimerResolution();
                WriteConfigMs(0.0);
                SetWindowTextA(hEditMs, "");
                SendMessage(hChkApplyStartup, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
                WriteBoolConfig(REG_VAL_APPLY, FALSE);
                SyncStartupShortcut(FALSE);
                RefreshInfo(hwndDlg);
                break;
            }

            if (wId == IDC_CHK_APPLY_STARTUP)
            {
                BOOL v = (BOOL)(SendMessage(hChkApplyStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
                WriteBoolConfig(REG_VAL_APPLY, v);
                SyncStartupShortcut(v);
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
            RemoveTrayIcon();
            EndDialog(hwndDlg, 0);
            return TRUE;
        }

        case WM_NCDESTROY:
        {
            if (hLineBrush) { DeleteObject(hLineBrush); hLineBrush = NULL; }
            return 0;
        }

        default:
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

    hBgBrush = CreateSolidBrush(clrBg);
    DialogBoxParamA(hInstance, MAKEINTRESOURCEA(1), NULL, MainDlgProc, 0);
    if (hBgBrush) DeleteObject(hBgBrush);

    CloseHandle(hMutex);
    return 0;
}
