#ifndef UNICODE
#define UNICODE
#endif

// --- Enable Visual Styles ---
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <Windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h> 
#include <ShellScalingApi.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <filesystem>

// Link necessary libraries
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib") 
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Gdi32.lib") 

// --- Constants & Colors ---
#define WM_ICON     0x1C0A
#define WM_OURICON  0x1C0B
#define HOTKEY_ID   1

// Custom messages
#define WM_UPDATE_HOTKEY (WM_USER + 1)
#define WM_PAUSE_HOTKEY  (WM_USER + 2)
#define WM_RESUME_HOTKEY (WM_USER + 3)

// Control IDs
#define ID_BTN_RESTORE_ALL    0x200
#define ID_LIST_WINDOWS       0x202
#define ID_HK_CONTROL         0x203
#define ID_LBL_CURRENT_HK     0x204
#define ID_BTN_MENU           0x205 
#define ID_BTN_CLOSE_SETTINGS 0x206 
#define ID_LBL_INSTR          0x208
#define ID_LBL_SETTINGS_TITLE 0x209 

#define ID_MENU_RESTORE_ALL   0x98
#define ID_MENU_EXIT          0x99
#define ID_MENU_OPEN_PREFS    0x100 

// --- MODERN DARK PALETTE ---
const COLORREF CLR_BG_DARK = RGB(30, 30, 30);
const COLORREF CLR_LIST_BG = RGB(40, 40, 40);
const COLORREF CLR_BORDER = RGB(60, 60, 60);

// Vibrant Blue for buttons
const COLORREF CLR_BTN_NORMAL = RGB(0, 110, 190);
const COLORREF CLR_BTN_HOVER = RGB(0, 130, 220);
const COLORREF CLR_BTN_PRESSED = RGB(0, 90, 160);

const COLORREF CLR_TEXT_WHITE = RGB(245, 245, 245);
const COLORREF CLR_TEXT_GRAY = RGB(160, 160, 160);
const COLORREF CLR_ICON_HOVER = RGB(55, 55, 55);

const std::wstring SAVE_FILE = L"TrayCaddy.dat";
const std::wstring SETTINGS_FILE = L"TrayCaddy.ini";

// --- Data Structures ---

struct HIDDEN_WINDOW {
    NOTIFYICONDATA icon = { 0 };
    HWND window = nullptr;
    UINT iconId = 0;
    std::wstring title;
    HICON hWindowIcon = nullptr;
};

struct CUSTOM_HOTKEY_DATA {
    UINT modifiers;
    UINT vKey;
};

struct APP_STATE {
    HWND mainWindow = nullptr;

    // Main View Controls
    HWND listView = nullptr;
    HWND btnRestore = nullptr;
    HWND btnMenu = nullptr;

    // Settings View Controls
    HWND btnCloseSettings = nullptr;
    HWND hkControl = nullptr;
    HWND lblSettingsTitle = nullptr;
    HWND lblCurrentHk = nullptr;
    HWND lblInstruction = nullptr;

    HMENU trayMenu = nullptr;
    std::vector<HIDDEN_WINDOW> hiddenWindows;
    UINT nextHiddenIconId = 1000;
    NOTIFYICONDATA mainIcon = { 0 };

    // GDI Objects
    HFONT hFontUi = nullptr;      // Standard text
    HFONT hFontBtn = nullptr;     // Button text (slightly bolder)
    HFONT hFontHeader = nullptr;  // Large Icons/Headers
    HBRUSH hBrushBg = nullptr;
    HIMAGELIST hImageList = nullptr;

    // UI State
    bool isSettingsOpen = false;

    // Hover States
    bool isHoverRestore = false;
    bool isHoverMenu = false;
    bool isHoverCloseSett = false;

    // Hotkey Settings
    UINT hkModifiers = MOD_WIN | MOD_SHIFT;
    UINT hkKey = 0x5A; // Default Z
};

// --- Forward Declarations ---
void SaveState(const APP_STATE* state);
void LoadSettings(APP_STATE* state);
void SaveSettings(const APP_STATE* state);
void UpdateAppHotkey(APP_STATE* state);
void RestoreWindow(APP_STATE* state, UINT iconId);
void RestoreAll(APP_STATE* state);
void MinimizeToTray(APP_STATE* state, HWND targetWindow = NULL);
void InitTrayIcon(HWND hWnd, HINSTANCE hInstance, NOTIFYICONDATA* icon);
void InitTrayMenu(HMENU* trayMenu);
void LoadState(APP_STATE* state);
void ReaddHiddenIcons(APP_STATE* state);
void UpdateListView(APP_STATE* state);
HFONT CreateModernFont(int pointSize, int weight);
void InvalidateButton(HWND hBtn);
void ToggleSettingsView(APP_STATE* state, bool showSettings);

// --- Hotkey Control Logic ---

std::wstring GetHotkeyString(UINT modifiers, UINT key) {
    std::wstring text = L"";
    if (modifiers & MOD_WIN)     text += L"Win + ";
    if (modifiers & MOD_CONTROL) text += L"Ctrl + ";
    if (modifiers & MOD_SHIFT)   text += L"Shift + ";
    if (modifiers & MOD_ALT)     text += L"Alt + ";

    if (key != 0) {
        wchar_t keyName[128] = { 0 };
        UINT scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);
        switch (key) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scanCode |= 0x100;
        }
        if (GetKeyNameText(scanCode << 16, keyName, 128)) text += keyName;
        else text += L"Unknown";
    }
    else text += L"None";
    return text;
}

LRESULT CALLBACK CustomHotkeySubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_SETFOCUS: PostMessage(GetParent(hWnd), WM_PAUSE_HOTKEY, 0, 0); break;
    case WM_KILLFOCUS: PostMessage(GetParent(hWnd), WM_RESUME_HOTKEY, 0, 0); break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        UINT modifiers = 0;
        if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) modifiers |= MOD_WIN;
        if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT) & 0x8000)   modifiers |= MOD_SHIFT;
        if (GetKeyState(VK_MENU) & 0x8000)    modifiers |= MOD_ALT;
        UINT key = (wParam != VK_CONTROL && wParam != VK_SHIFT && wParam != VK_MENU && wParam != VK_LWIN && wParam != VK_RWIN) ? (UINT)wParam : 0;
        SetWindowText(hWnd, GetHotkeyString(modifiers, key).c_str());
        CUSTOM_HOTKEY_DATA* data = (CUSTOM_HOTKEY_DATA*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (data) { data->modifiers = modifiers; data->vKey = key; }
        return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP: {
        CUSTOM_HOTKEY_DATA* data = (CUSTOM_HOTKEY_DATA*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (data && data->vKey != 0) SendMessage(GetParent(hWnd), WM_UPDATE_HOTKEY, 0, 0);
        return 0;
    }
    case WM_CHAR: case WM_SYSCHAR: return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_TEXT_WHITE);
        SetBkColor(hdc, CLR_LIST_BG);
        static HBRUSH hBrushEdit = CreateSolidBrush(CLR_LIST_BG);
        return (LRESULT)hBrushEdit;
    }
    case WM_NCDESTROY:
        CUSTOM_HOTKEY_DATA* data = (CUSTOM_HOTKEY_DATA*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (data) delete data;
        RemoveWindowSubclass(hWnd, CustomHotkeySubclass, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void MakeCustomHotkeyControl(HWND hEdit, UINT defaultMod, UINT defaultKey) {
    CUSTOM_HOTKEY_DATA* data = new CUSTOM_HOTKEY_DATA();
    data->modifiers = defaultMod;
    data->vKey = defaultKey;
    SetWindowLongPtr(hEdit, GWLP_USERDATA, (LONG_PTR)data);
    SetWindowSubclass(hEdit, CustomHotkeySubclass, 0, 0);
    SetWindowText(hEdit, GetHotkeyString(defaultMod, defaultKey).c_str());
}

// --- Logic Implementation ---

void SaveState(const APP_STATE* state) {
    if (state->hiddenWindows.empty()) { DeleteFile(SAVE_FILE.c_str()); return; }
    std::wofstream file(SAVE_FILE, std::ios::trunc);
    if (!file.is_open()) return;
    for (const auto& item : state->hiddenWindows) {
        if (item.window && IsWindow(item.window)) file << (uintptr_t)item.window << L"\n";
    }
}

void SaveSettings(const APP_STATE* state) {
    WritePrivateProfileString(L"Settings", L"Key", std::to_wstring(state->hkKey).c_str(), SETTINGS_FILE.c_str());
    WritePrivateProfileString(L"Settings", L"Modifiers", std::to_wstring(state->hkModifiers).c_str(), SETTINGS_FILE.c_str());
}

void LoadSettings(APP_STATE* state) {
    state->hkKey = GetPrivateProfileInt(L"Settings", L"Key", 0x5A, SETTINGS_FILE.c_str());
    state->hkModifiers = GetPrivateProfileInt(L"Settings", L"Modifiers", MOD_WIN | MOD_SHIFT, SETTINGS_FILE.c_str());
}

void UpdateAppHotkey(APP_STATE* state) {
    UnregisterHotKey(state->mainWindow, HOTKEY_ID);
    RegisterHotKey(state->mainWindow, HOTKEY_ID, state->hkModifiers | MOD_NOREPEAT, state->hkKey);
}

void ReaddHiddenIcons(APP_STATE* state) {
    if (!state) return;
    state->hiddenWindows.erase(std::remove_if(state->hiddenWindows.begin(), state->hiddenWindows.end(),
        [](const HIDDEN_WINDOW& item) { return !item.window || !IsWindow(item.window); }), state->hiddenWindows.end());
    for (auto& item : state->hiddenWindows) {
        Shell_NotifyIcon(NIM_ADD, &item.icon);
        item.icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon(NIM_SETVERSION, &item.icon);
    }
    UpdateListView(state);
}

void UpdateListView(APP_STATE* state) {
    if (!state->listView) return;
    ListView_DeleteAllItems(state->listView);
    ImageList_RemoveAll(state->hImageList);

    int index = 0;
    for (auto& item : state->hiddenWindows) {
        int imgIdx = -1;
        if (item.hWindowIcon) imgIdx = ImageList_AddIcon(state->hImageList, item.hWindowIcon);
        else imgIdx = ImageList_AddIcon(state->hImageList, LoadIcon(NULL, IDI_APPLICATION));

        LVITEM lvItem = { 0 };
        lvItem.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvItem.iItem = index;
        lvItem.iSubItem = 0;
        lvItem.pszText = const_cast<LPWSTR>(item.title.empty() ? L"Unknown Window" : item.title.c_str());
        lvItem.lParam = (LPARAM)item.iconId;
        lvItem.iImage = imgIdx;
        ListView_InsertItem(state->listView, &lvItem);
        index++;
    }
    InvalidateRect(state->listView, NULL, TRUE);
}

void RestoreWindow(APP_STATE* state, UINT iconId) {
    auto it = std::find_if(state->hiddenWindows.begin(), state->hiddenWindows.end(),
        [&](const HIDDEN_WINDOW& item) { return item.iconId == iconId; });
    if (it != state->hiddenWindows.end()) {
        if (it->window && IsWindow(it->window)) {
            ShowWindow(it->window, SW_RESTORE);
            SetForegroundWindow(it->window);
        }
        Shell_NotifyIcon(NIM_DELETE, const_cast<PNOTIFYICONDATA>(&it->icon));
        if (it->hWindowIcon) DestroyIcon(it->hWindowIcon);
        state->hiddenWindows.erase(it);
        SaveState(state);
        UpdateListView(state);
    }
}

void RestoreAll(APP_STATE* state) {
    for (auto& item : state->hiddenWindows) {
        if (item.window && IsWindow(item.window)) ShowWindow(item.window, SW_RESTORE);
        Shell_NotifyIcon(NIM_DELETE, &item.icon);
        if (item.hWindowIcon) DestroyIcon(item.hWindowIcon);
    }
    state->hiddenWindows.clear();
    SaveState(state);
    UpdateListView(state);
}

void MinimizeToTray(APP_STATE* state, HWND targetWindow) {
    const wchar_t* restrictWins[] = { L"WorkerW", L"Shell_TrayWnd", L"Progman" };
    HWND currWin = targetWindow ? targetWindow : GetForegroundWindow();
    if (!currWin || !IsWindow(currWin) || currWin == state->mainWindow) return;

    wchar_t className[256];
    if (GetClassName(currWin, className, 256)) {
        for (const auto& restricted : restrictWins) if (wcscmp(restricted, className) == 0) return;
    }

    HICON hIcon = (HICON)SendMessage(currWin, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(currWin, GCLP_HICONSM);
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
    HICON hStorageIcon = CopyIcon(hIcon);

    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = state->mainWindow;
    nid.hIcon = hIcon;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_ICON;
    nid.uID = state->nextHiddenIconId++;
    wchar_t titleBuf[128];
    GetWindowText(currWin, titleBuf, 128);
    wcscpy_s(nid.szTip, titleBuf);

    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        ShowWindow(currWin, SW_HIDE);
        HIDDEN_WINDOW newItem;
        newItem.icon = nid;
        newItem.window = currWin;
        newItem.iconId = nid.uID;
        newItem.title = std::wstring(titleBuf);
        newItem.hWindowIcon = hStorageIcon;
        state->hiddenWindows.push_back(newItem);
        if (!targetWindow) SaveState(state);
        UpdateListView(state);
    }
}

void InitTrayIcon(HWND hWnd, HINSTANCE hInstance, NOTIFYICONDATA* icon) {
    icon->cbSize = sizeof(NOTIFYICONDATA);
    icon->hWnd = hWnd;
    icon->hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    if (!icon->hIcon) icon->hIcon = LoadIcon(NULL, IDI_APPLICATION);
    icon->uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
    icon->uID = 1;
    icon->uCallbackMessage = WM_OURICON;
    wcscpy_s(icon->szTip, L"TrayCaddy");
    Shell_NotifyIcon(NIM_ADD, icon);
    icon->uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, icon);
}

void InitTrayMenu(HMENU* trayMenu) {
    *trayMenu = CreatePopupMenu();
    InsertMenu(*trayMenu, 0, MF_BYPOSITION | MF_STRING, ID_MENU_RESTORE_ALL, L"Restore all windows");
    InsertMenu(*trayMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(*trayMenu, 2, MF_BYPOSITION | MF_STRING, ID_MENU_EXIT, L"Exit");
}

void LoadState(APP_STATE* state) {
    if (!std::filesystem::exists(SAVE_FILE)) return;
    std::wifstream file(SAVE_FILE);
    std::wstring line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            uintptr_t val = std::stoull(line);
            HWND hwnd = (HWND)val;
            if (IsWindow(hwnd)) MinimizeToTray(state, hwnd);
        }
        catch (...) {}
    }
    UpdateListView(state);
}

// --- UI Logic & Rendering ---

HFONT CreateModernFont(int pointSize, int weight) {
    HDC hdc = GetDC(NULL);
    LONG height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return CreateFont(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void InvalidateButton(HWND hBtn) {
    InvalidateRect(hBtn, NULL, FALSE);
}

void ToggleSettingsView(APP_STATE* state, bool showSettings) {
    state->isSettingsOpen = showSettings;
    int mainShow = showSettings ? SW_HIDE : SW_SHOW;
    int settShow = showSettings ? SW_SHOW : SW_HIDE;

    // Toggle Main Controls
    ShowWindow(state->listView, mainShow);
    ShowWindow(state->btnRestore, mainShow);
    ShowWindow(state->btnMenu, mainShow);

    // Toggle Settings Controls
    ShowWindow(state->hkControl, settShow);
    ShowWindow(state->btnCloseSettings, settShow);
    ShowWindow(state->lblSettingsTitle, settShow);
    ShowWindow(state->lblCurrentHk, settShow);
    ShowWindow(state->lblInstruction, settShow);

    // Ensure focus is safe
    if (showSettings) SetFocus(state->hkControl);
    else SetFocus(state->listView);
}

void DrawModernButton(LPDRAWITEMSTRUCT pDIS, const APP_STATE* state) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    bool isPressed = (pDIS->itemState & ODS_SELECTED);
    bool isHovered = false;

    int id = pDIS->CtlID;
    if (id == ID_BTN_RESTORE_ALL) isHovered = state->isHoverRestore;
    else if (id == ID_BTN_MENU) isHovered = state->isHoverMenu;
    else if (id == ID_BTN_CLOSE_SETTINGS) isHovered = state->isHoverCloseSett;

    FillRect(hdc, &rc, state->hBrushBg); // Clear background

    // Style 1: Flat Icon Button (Menu / Close) - No background unless hovered
    if (id == ID_BTN_MENU || id == ID_BTN_CLOSE_SETTINGS) {
        if (isHovered || isPressed) {
            // Circle or Rounded Square background for icons
            HBRUSH hHover = CreateSolidBrush(CLR_ICON_HOVER);
            HPEN hPenNull = CreatePen(PS_NULL, 0, 0);
            SelectObject(hdc, hHover);
            SelectObject(hdc, hPenNull);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            DeleteObject(hHover);
            DeleteObject(hPenNull);
        }
        SetTextColor(hdc, CLR_TEXT_WHITE);
        SelectObject(hdc, state->hFontHeader); // Use larger font for icons
    }
    // Style 2: Standard Rounded CTA Button
    else {
        COLORREF btnColor = isPressed ? CLR_BTN_PRESSED : (isHovered ? CLR_BTN_HOVER : CLR_BTN_NORMAL);
        HBRUSH hBr = CreateSolidBrush(btnColor);
        HPEN hPen = CreatePen(PS_NULL, 0, 0);

        SelectObject(hdc, hBr);
        SelectObject(hdc, hPen);

        // Draw Rounded Rectangle
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

        DeleteObject(hBr);
        DeleteObject(hPen);

        SetTextColor(hdc, CLR_TEXT_WHITE);
        SelectObject(hdc, state->hFontBtn);
        if (isPressed) OffsetRect(&rc, 1, 1);
    }

    SetBkMode(hdc, TRANSPARENT);
    wchar_t buf[256];
    GetWindowText(pDIS->hwndItem, buf, 256);
    DrawText(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT HandleListCustomDraw(APP_STATE* state, LPARAM lParam) {
    if (state->hiddenWindows.empty()) {
        HDC hdc = GetDC(state->listView);
        RECT rc; GetClientRect(state->listView, &rc);
        SetBkColor(hdc, CLR_LIST_BG);
        SetTextColor(hdc, CLR_TEXT_GRAY);
        SelectObject(hdc, state->hFontUi);
        const wchar_t* msg = L"No hidden windows.\nUse the hotkey to hide active window.";
        DrawText(hdc, msg, -1, &rc, DT_CENTER | DT_VCENTER);
        ReleaseDC(state->listView, hdc);
    }
    return 0;
}

// --- Window Procedure ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    APP_STATE* state = (APP_STATE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        state = (APP_STATE*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);
    }

    static UINT s_taskbarCreatedMsg = 0;
    if (s_taskbarCreatedMsg == 0) s_taskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");
    if (s_taskbarCreatedMsg != 0 && uMsg == s_taskbarCreatedMsg && state) {
        InitTrayIcon(hwnd, GetModuleHandle(NULL), &state->mainIcon);
        ReaddHiddenIcons(state);
        return 0;
    }

    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icex);

        // We use GetClientRect for layout to ensure robustness
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int clientW = rcClient.right;
        int clientH = rcClient.bottom;

        const int margin = 24; // Nice padding

        // Fonts
        state->hFontUi = CreateModernFont(10, FW_NORMAL);
        state->hFontBtn = CreateModernFont(10, FW_SEMIBOLD);
        state->hFontHeader = CreateModernFont(16, FW_NORMAL);

        // --- MAIN PAGE ---

        // Menu Button (Top Left)
        state->btnMenu = CreateWindow(L"BUTTON", L"\u22EE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            margin, 16, 32, 32, hwnd, (HMENU)ID_BTN_MENU, GetModuleHandle(NULL), NULL);

        // List View
        int listTop = 64;
        int footerBtnHeight = 35;
        // Calculate height based on actual client height to avoid cutoff
        int listH = clientH - margin - footerBtnHeight - 20 - listTop;

        state->listView = CreateWindow(WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
            margin, listTop, clientW - (margin * 2), listH,
            hwnd, (HMENU)ID_LIST_WINDOWS, GetModuleHandle(NULL), NULL);

        int iconSize = GetSystemMetrics(SM_CXSMICON);
        state->hImageList = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, 1, 1);
        ListView_SetImageList(state->listView, state->hImageList, LVSIL_SMALL);

        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = clientW - (margin * 2) - 20;
        lvc.pszText = (LPWSTR)L"Window Title";
        ListView_InsertColumn(state->listView, 0, &lvc);

        ListView_SetExtendedListViewStyle(state->listView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(state->listView, CLR_LIST_BG);
        ListView_SetTextBkColor(state->listView, CLR_LIST_BG);
        ListView_SetTextColor(state->listView, CLR_TEXT_WHITE);

        // Footer Buttons - Only "Restore All" remains, centered in footer space
        int btnW = 120;
        int footerY = clientH - footerBtnHeight - margin;
        int btnX = (clientW - btnW) / 2; // Center horizontally

        state->btnRestore = CreateWindow(L"BUTTON", L"Restore All", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            btnX, footerY, btnW, footerBtnHeight, hwnd, (HMENU)ID_BTN_RESTORE_ALL, GetModuleHandle(NULL), NULL);

        // --- SETTINGS PAGE ---

        state->lblSettingsTitle = CreateWindow(L"STATIC", L"Settings", WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
            margin, 16, 200, 32, hwnd, (HMENU)ID_LBL_SETTINGS_TITLE, GetModuleHandle(NULL), NULL);

        // Close Button (Top Right)
        state->btnCloseSettings = CreateWindow(L"BUTTON", L"\u2715", WS_CHILD | BS_PUSHBUTTON | BS_OWNERDRAW,
            clientW - 32 - margin, 16, 32, 32, hwnd, (HMENU)ID_BTN_CLOSE_SETTINGS, GetModuleHandle(NULL), NULL);

        int setY = 90;
        state->lblInstruction = CreateWindow(L"STATIC", L"Click the box below and press a key combination:", WS_CHILD | SS_LEFT,
            margin, setY, 350, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);

        setY += 30;
        state->hkControl = CreateWindow(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_CENTER | ES_READONLY,
            margin, setY, 250, 30, hwnd, (HMENU)ID_HK_CONTROL, GetModuleHandle(NULL), NULL);

        setY += 50;
        state->lblCurrentHk = CreateWindow(L"STATIC", L"", WS_CHILD | SS_LEFT,
            margin, setY, 300, 20, hwnd, (HMENU)ID_LBL_CURRENT_HK, GetModuleHandle(NULL), NULL);

        return 0;
    }

    case WM_SETFONT: {
        HFONT hFont = (HFONT)wParam;
        EnumChildWindows(hwnd, [](HWND child, LPARAM lparam) -> BOOL {
            int id = GetDlgCtrlID(child);
            APP_STATE* s = (APP_STATE*)GetWindowLongPtr(GetParent(child), GWLP_USERDATA);

            if (!s) return TRUE;

            if (id == ID_BTN_MENU || id == ID_BTN_CLOSE_SETTINGS) {
                SendMessage(child, WM_SETFONT, (WPARAM)s->hFontHeader, TRUE);
            }
            else if (id == ID_LBL_SETTINGS_TITLE) {
                SendMessage(child, WM_SETFONT, (WPARAM)s->hFontHeader, TRUE);
            }
            else if (id == ID_BTN_RESTORE_ALL) {
                SendMessage(child, WM_SETFONT, (WPARAM)s->hFontBtn, TRUE);
            }
            else {
                SendMessage(child, WM_SETFONT, (WPARAM)s->hFontUi, TRUE);
            }
            return TRUE;
            }, (LPARAM)hFont);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, state->hBrushBg);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        wchar_t buff[32];
        GetWindowText((HWND)lParam, buff, 32);
        if (wcscmp(buff, L"Settings") == 0) {
            SelectObject(hdc, state->hFontHeader);
            SetTextColor(hdc, CLR_TEXT_WHITE);
        }
        else {
            SetTextColor(hdc, CLR_TEXT_WHITE);
        }
        return (LRESULT)state->hBrushBg;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawModernButton(pDIS, state);
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        auto CheckHover = [&](HWND hBtn, bool& currentFlag) {
            RECT rc; GetWindowRect(hBtn, &rc);
            MapWindowPoints(NULL, hwnd, (LPPOINT)&rc, 2);
            bool isHover = PtInRect(&rc, pt);
            if (isHover != currentFlag) {
                currentFlag = isHover;
                InvalidateButton(hBtn);
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
            };

        if (state->isSettingsOpen) {
            CheckHover(state->btnCloseSettings, state->isHoverCloseSett);
        }
        else {
            CheckHover(state->btnRestore, state->isHoverRestore);
            CheckHover(state->btnMenu, state->isHoverMenu);
        }
        break;
    }

    case WM_MOUSELEAVE:
        state->isHoverRestore = state->isHoverMenu = state->isHoverCloseSett = false;
        InvalidateRect(hwnd, NULL, FALSE);
        break;

    case WM_UPDATE_HOTKEY: {
        if (!state || !state->hkControl) break;
        CUSTOM_HOTKEY_DATA* data = (CUSTOM_HOTKEY_DATA*)GetWindowLongPtr(state->hkControl, GWLP_USERDATA);
        if (data && data->vKey != 0) {
            state->hkKey = data->vKey;
            state->hkModifiers = data->modifiers;
            SaveSettings(state);
            std::wstring newLabel = L"Saved: " + GetHotkeyString(state->hkModifiers, state->hkKey);
            SetWindowText(state->lblCurrentHk, newLabel.c_str());
        }
        break;
    }
    case WM_PAUSE_HOTKEY: if (state) UnregisterHotKey(state->mainWindow, HOTKEY_ID); break;
    case WM_RESUME_HOTKEY: if (state) UpdateAppHotkey(state); break;

    case WM_ICON: if (state && lParam == WM_LBUTTONDBLCLK) RestoreWindow(state, (UINT)wParam); break;
    case WM_OURICON:
        if (!state) break;
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
        else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(state->trayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        // Handle Exit from Menu
        if (id == ID_MENU_EXIT) {
            PostQuitMessage(0);
            return 0;
        }

        if (!state) break;

        // --- Navigation Logic ---
        if (id == ID_BTN_MENU) {
            HMENU hPop = CreatePopupMenu();
            AppendMenu(hPop, MF_STRING, ID_MENU_OPEN_PREFS, L"Preferences");
            AppendMenu(hPop, MF_SEPARATOR, 0, NULL);
            AppendMenu(hPop, MF_STRING, ID_MENU_EXIT, L"Exit");

            RECT rc; GetWindowRect(state->btnMenu, &rc);
            // Show below button
            int selection = TrackPopupMenu(hPop, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                rc.left, rc.bottom, 0, hwnd, NULL);

            if (selection == ID_MENU_OPEN_PREFS) {
                std::wstring cur = L"Current: " + GetHotkeyString(state->hkModifiers, state->hkKey);
                SetWindowText(state->lblCurrentHk, cur.c_str());
                ToggleSettingsView(state, true);
            }
            else if (selection == ID_MENU_EXIT) {
                PostQuitMessage(0);
            }
            DestroyMenu(hPop);
        }

        if (id == ID_BTN_CLOSE_SETTINGS) {
            ToggleSettingsView(state, false);
        }

        if (id == ID_BTN_RESTORE_ALL || id == ID_MENU_RESTORE_ALL) RestoreAll(state);
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (!state) break;
        if (lpnmh->idFrom == ID_LIST_WINDOWS && lpnmh->code == NM_CUSTOMDRAW) HandleListCustomDraw(state, lParam);
        if (lpnmh->idFrom == ID_LIST_WINDOWS && lpnmh->code == NM_DBLCLK) {
            LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
            if (lpnmitem->iItem != -1) {
                LVITEM item = { 0 }; item.iItem = lpnmitem->iItem; item.mask = LVIF_PARAM;
                ListView_GetItem(state->listView, &item);
                RestoreWindow(state, (UINT)item.lParam);
            }
        }
        break;
    }
    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_HOTKEY: if (state && wParam == HOTKEY_ID) MinimizeToTray(state, NULL); break;
    default: return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"TrayCaddy_Unique_Mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) return 1;

    APP_STATE* appState = new APP_STATE();
    LoadSettings(appState);
    appState->hBrushBg = CreateSolidBrush(CLR_BG_DARK);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TrayCaddy";
    wc.hbrBackground = appState->hBrushBg;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // --- CALCULATE CORRECT WINDOW SIZE ---
    // We want exactly 360x280 of usable client area (shorter vertically).
    RECT rc = { 0, 0, 360, 280 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    // Use SPI_GETWORKAREA to get usable screen space (excluding taskbar)
    RECT rcWork;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

    // Bottom right positioning with padding
    int padding = 20;
    int x = rcWork.right - w - padding;
    int y = rcWork.bottom - h - padding;

    appState->mainWindow = CreateWindow(L"TrayCaddy", L"TrayCaddy", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h, NULL, NULL, hInstance, appState);

    if (!appState->mainWindow) return 1;

    MakeCustomHotkeyControl(appState->hkControl, appState->hkModifiers, appState->hkKey);
    SendMessage(appState->mainWindow, WM_SETFONT, (WPARAM)appState->hFontUi, TRUE);
    InitTrayIcon(appState->mainWindow, hInstance, &appState->mainIcon);
    InitTrayMenu(&appState->trayMenu);
    UpdateAppHotkey(appState);
    LoadState(appState);
    ShowWindow(appState->mainWindow, SW_SHOW);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    RestoreAll(appState);
    Shell_NotifyIcon(NIM_DELETE, &appState->mainIcon);
    UnregisterHotKey(appState->mainWindow, HOTKEY_ID);
    if (appState->trayMenu) DestroyMenu(appState->trayMenu);

    if (appState->hFontUi) DeleteObject(appState->hFontUi);
    if (appState->hFontBtn) DeleteObject(appState->hFontBtn);
    if (appState->hFontHeader) DeleteObject(appState->hFontHeader);

    if (appState->hBrushBg) DeleteObject(appState->hBrushBg);
    if (appState->hImageList) ImageList_Destroy(appState->hImageList);
    if (hMutex) CloseHandle(hMutex);
    delete appState;
    return (int)msg.wParam;
}