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

// Control IDs
#define ID_BTN_RESTORE_ALL  0x200
#define ID_BTN_EXIT         0x201
#define ID_LIST_WINDOWS     0x202
#define ID_HK_CONTROL       0x203
#define ID_BTN_APPLY_HK     0x205

#define ID_MENU_RESTORE_ALL 0x98
#define ID_MENU_EXIT        0x99

// --- DARKER COLOR PALETTE ---
// Main Window Background (Deep Charcoal Blue)
const COLORREF CLR_BG_DARK = RGB(40, 50, 65);
// Button Color (Dark Teal)
const COLORREF CLR_BTN_TEAL = RGB(0, 95, 105);
// Button Pressed Color (Very Dark Teal)
const COLORREF CLR_BTN_PRESSED = RGB(0, 70, 80);
// Main Text Color (White for contrast)
const COLORREF CLR_TEXT_WHITE = RGB(255, 255, 255);
// List View Background (Slightly lighter than main BG for separation)
const COLORREF CLR_LIST_BG = RGB(55, 65, 80);
// List View Text (Off-White)
const COLORREF CLR_LIST_TEXT = RGB(235, 235, 235);

const std::wstring SAVE_FILE = L"TrayCaddy.dat";
const std::wstring SETTINGS_FILE = L"TrayCaddy.ini";

// --- Data Structures ---

struct HIDDEN_WINDOW {
    NOTIFYICONDATA icon = { 0 };
    HWND window = nullptr;
    UINT iconId = 0;
    std::wstring title;
};

struct CUSTOM_HOTKEY_DATA {
    UINT modifiers;
    UINT vKey;
};

struct APP_STATE {
    HWND mainWindow = nullptr;
    HWND listView = nullptr;
    HWND hkControl = nullptr;
    HMENU trayMenu = nullptr;

    std::vector<HIDDEN_WINDOW> hiddenWindows;
    UINT nextHiddenIconId = 1000;
    NOTIFYICONDATA mainIcon = { 0 };

    // GDI Objects
    HFONT hFontUi = nullptr;
    HBRUSH hBrushBg = nullptr; // Brush for the main background color

    // Hotkey Settings
    UINT hkModifiers = MOD_WIN | MOD_SHIFT;
    UINT hkKey = 0x5A;
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
HFONT CreateModernFont(int pointSize, bool bold);

// --- Custom Hotkey Control ---

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
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT modifiers = 0;
        if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) modifiers |= MOD_WIN;
        if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT) & 0x8000)   modifiers |= MOD_SHIFT;
        if (GetKeyState(VK_MENU) & 0x8000)    modifiers |= MOD_ALT;

        UINT key = 0;
        if (wParam != VK_CONTROL && wParam != VK_SHIFT && wParam != VK_MENU && wParam != VK_LWIN && wParam != VK_RWIN) {
            key = (UINT)wParam;
        }

        std::wstring text = GetHotkeyString(modifiers, key);
        SetWindowText(hWnd, text.c_str());

        CUSTOM_HOTKEY_DATA* data = (CUSTOM_HOTKEY_DATA*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (data) {
            data->modifiers = modifiers;
            data->vKey = key;
        }
        return 0;
    }
    case WM_CHAR: case WM_SYSCHAR: case WM_KEYUP: case WM_SYSKEYUP:
        return 0;
        // Paint the hotkey edit control background dark
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        // Use the List BG color for edit box background contrast
        HBRUSH hBr = CreateSolidBrush(CLR_LIST_BG);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);
        return 1;
    }
    case WM_CTLCOLORSTATIC: // For read-only edit control
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_TEXT_WHITE);
        SetBkColor(hdc, CLR_LIST_BG);
        // Return a brush for the background
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
    if (state->hiddenWindows.empty()) {
        DeleteFile(SAVE_FILE.c_str());
        return;
    }
    std::wofstream file(SAVE_FILE, std::ios::trunc);
    if (!file.is_open()) return;
    for (const auto& item : state->hiddenWindows) {
        if (item.window && IsWindow(item.window)) {
            file << (uintptr_t)item.window << L"\n";
        }
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
    if (!RegisterHotKey(state->mainWindow, HOTKEY_ID, state->hkModifiers | MOD_NOREPEAT, state->hkKey)) {
        MessageBox(state->mainWindow, L"Failed to register hotkey.", L"Error", MB_ICONWARNING);
    }
}

void ReaddHiddenIcons(APP_STATE* state) {
    if (!state) return;
    state->hiddenWindows.erase(
        std::remove_if(state->hiddenWindows.begin(), state->hiddenWindows.end(),
            [](const HIDDEN_WINDOW& item) { return !item.window || !IsWindow(item.window); }),
        state->hiddenWindows.end());

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
    LVITEM lvItem = { 0 };
    lvItem.mask = LVIF_TEXT | LVIF_PARAM;
    int index = 0;
    for (const auto& item : state->hiddenWindows) {
        lvItem.iItem = index;
        lvItem.iSubItem = 0;
        lvItem.pszText = const_cast<LPWSTR>(item.title.empty() ? L"Unknown Window" : item.title.c_str());
        lvItem.lParam = (LPARAM)item.iconId;
        ListView_InsertItem(state->listView, &lvItem);
        index++;
    }
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
        state->hiddenWindows.erase(it);
        SaveState(state);
        UpdateListView(state);
    }
}

void RestoreAll(APP_STATE* state) {
    for (auto& item : state->hiddenWindows) {
        if (item.window && IsWindow(item.window)) ShowWindow(item.window, SW_RESTORE);
        Shell_NotifyIcon(NIM_DELETE, &item.icon);
    }
    state->hiddenWindows.clear();
    SaveState(state);
    UpdateListView(state);
}

void MinimizeToTray(APP_STATE* state, HWND targetWindow) {
    const wchar_t* restrictWins[] = { L"WorkerW", L"Shell_TrayWnd", L"Progman" };
    HWND currWin = targetWindow ? targetWindow : GetForegroundWindow();

    if (!currWin || !IsWindow(currWin)) return;
    if (currWin == state->mainWindow) return;

    wchar_t className[256];
    if (GetClassName(currWin, className, 256)) {
        for (const auto& restricted : restrictWins) {
            if (wcscmp(restricted, className) == 0) return;
        }
    }

    HICON hIcon = (HICON)GetClassLongPtr(currWin, GCLP_HICONSM);
    if (!hIcon) hIcon = (HICON)SendMessage(currWin, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);

    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = state->mainWindow;
    nid.hIcon = hIcon;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_ICON;
    const UINT iconId = state->nextHiddenIconId++;
    nid.uID = iconId;

    wchar_t titleBuf[128];
    GetWindowText(currWin, titleBuf, 128);
    wcscpy_s(nid.szTip, titleBuf);

    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        ShowWindow(currWin, SW_HIDE);
        HIDDEN_WINDOW newItem;
        newItem.icon = nid;
        newItem.window = currWin;
        newItem.iconId = iconId;
        newItem.title = std::wstring(titleBuf);
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
    InsertMenu(*trayMenu, 1, MF_BYPOSITION | MF_STRING, ID_MENU_EXIT, L"Exit");
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

// --- Visual & Helper Functions ---

HFONT CreateModernFont(int pointSize, bool bold) {
    HDC hdc = GetDC(NULL);
    LONG height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);

    return CreateFont(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// Draw a modern, dark teal, rounded button (with white corner fix)
void DrawModernButton(LPDRAWITEMSTRUCT pDIS, const APP_STATE* state) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    bool isPressed = (pDIS->itemState & ODS_SELECTED);

    // --- FIX FOR CORNERS ---
    // Fill the button bounding box with the window's dark background color first.
    // This ensures the corners outside the rounded rectangle blend in.
    FillRect(hdc, &rc, state->hBrushBg);

    // Setup GDI
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_WHITE);

    // Determine button color based on state using darker palette
    COLORREF btnColor = isPressed ? CLR_BTN_PRESSED : CLR_BTN_TEAL;
    HBRUSH hBr = CreateSolidBrush(btnColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, btnColor);

    HGDIOBJ oldBr = SelectObject(hdc, hBr);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    // Draw Rounded Rectangle
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);

    // Get Text
    wchar_t buf[256];
    GetWindowText(pDIS->hwndItem, buf, 256);

    // Draw Text Centered (with slight offset if pressed)
    if (isPressed) OffsetRect(&rc, 1, 1);
    DrawText(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Cleanup
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);
}

// --- Window Procedure ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    APP_STATE* state = (APP_STATE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    static UINT s_taskbarCreatedMsg = 0;
    if (s_taskbarCreatedMsg == 0) s_taskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");

    if (s_taskbarCreatedMsg != 0 && uMsg == s_taskbarCreatedMsg) {
        if (state) {
            InitTrayIcon(hwnd, GetModuleHandle(NULL), &state->mainIcon);
            ReaddHiddenIcons(state);
        }
        return 0;
    }

    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);

        // Layout Constants
        const int margin = 20;
        const int winWidth = 450;
        const int winHeight = 400;

        SetWindowPos(hwnd, NULL, 0, 0, winWidth, winHeight, SWP_NOMOVE | SWP_NOZORDER);

        // 1. LIST VIEW 
        int listHeight = 220;
        HWND hList = CreateWindow(WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            margin, margin, winWidth - (margin * 2 + 15), listHeight,
            hwnd, (HMENU)ID_LIST_WINDOWS, GetModuleHandle(NULL), NULL);

        LVCOLUMN lvc;
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = winWidth - (margin * 2) - 40;
        lvc.pszText = (LPWSTR)L"Window Title";
        ListView_InsertColumn(hList, 0, &lvc);

        // Modern Dark List Styles
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(hList, CLR_LIST_BG);     // Dark list background
        ListView_SetTextBkColor(hList, CLR_LIST_BG); // Dark text background
        ListView_SetTextColor(hList, CLR_LIST_TEXT); // Light text

        int currentY = margin + listHeight + 15;

        // 2. SETTINGS SECTION
        int rowHeight = 28;

        // Header Label
        CreateWindow(L"STATIC", L"Activation Hotkey", WS_CHILD | WS_VISIBLE | SS_LEFT,
            margin, currentY, 200, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);

        currentY += 25;

        // Hotkey Display (ReadOnly Edit)
        // Note: Subclassing handles dark coloring for this
        CreateWindow(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_READONLY,
            margin, currentY, 200, rowHeight,
            hwnd, (HMENU)ID_HK_CONTROL, GetModuleHandle(NULL), NULL);

        // Apply Button
        CreateWindow(L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            margin + 210, currentY, 80, rowHeight,
            hwnd, (HMENU)ID_BTN_APPLY_HK, GetModuleHandle(NULL), NULL);

        // 3. FOOTER BUTTONS
        int btnWidth = 120;
        int btnHeight = 35;
        int bottomY = winHeight - btnHeight - 45;

        CreateWindow(L"BUTTON", L"Restore All",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            margin, bottomY, btnWidth, btnHeight,
            hwnd, (HMENU)ID_BTN_RESTORE_ALL, GetModuleHandle(NULL), NULL);

        CreateWindow(L"BUTTON", L"Exit",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            winWidth - btnWidth - margin - 15, bottomY, btnWidth, btnHeight,
            hwnd, (HMENU)ID_BTN_EXIT, GetModuleHandle(NULL), NULL);

        return 0;
    }

    case WM_SETFONT: {
        HFONT hFont = (HFONT)wParam;
        EnumChildWindows(hwnd, [](HWND child, LPARAM lparam) -> BOOL {
            SendMessage(child, WM_SETFONT, (WPARAM)lparam, TRUE);
            return TRUE;
            }, (LPARAM)hFont);
        return 0;
    }

                   // --- Modern Dark Rendering ---

                   // Paint the Main Window Background Dark
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, state->hBrushBg);
        return 1;
    }

                      // Handle Static Text Transparency (White text on dark background)
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT_WHITE);
        return (LRESULT)state->hBrushBg;
    }

                          // Draw Buttons manually
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawModernButton(pDIS, state);
            return TRUE;
        }
        break;
    }

                    // --- Logic Handlers ---

    case WM_ICON:
        if (!state) break;
        if (lParam == WM_LBUTTONDBLCLK) RestoreWindow(state, (UINT)wParam);
        break;

    case WM_OURICON:
        if (!state) break;
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(state->trayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_EXIT || id == ID_MENU_EXIT) {
            PostQuitMessage(0);
            return 0;
        }
        if (!state) break;

        if (id == ID_BTN_RESTORE_ALL || id == ID_MENU_RESTORE_ALL) {
            RestoreAll(state);
        }
        else if (id == ID_BTN_APPLY_HK) {
            CUSTOM_HOTKEY_DATA* data = (CUSTOM_HOTKEY_DATA*)GetWindowLongPtr(state->hkControl, GWLP_USERDATA);
            if (data && data->vKey != 0) {
                state->hkKey = data->vKey;
                state->hkModifiers = data->modifiers;
                UpdateAppHotkey(state);
                SaveSettings(state);
                MessageBox(hwnd, L"Hotkey updated successfully.", L"TrayCaddy", MB_OK);
            }
            else {
                MessageBox(hwnd, L"Please enter a valid key combination.", L"Info", MB_OK);
            }
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (!state) break;
        if (lpnmh->idFrom == ID_LIST_WINDOWS && lpnmh->code == NM_DBLCLK) {
            LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
            if (lpnmitem->iItem != -1) {
                LVITEM item = { 0 };
                item.iItem = lpnmitem->iItem;
                item.mask = LVIF_PARAM;
                ListView_GetItem(state->listView, &item);
                RestoreWindow(state, (UINT)item.lParam);
            }
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_HOTKEY:
        if (state && wParam == HOTKEY_ID) MinimizeToTray(state, NULL);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE hMutex = CreateMutex(NULL, TRUE, L"TrayCaddy_Unique_Mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    APP_STATE* appState = new APP_STATE();
    LoadSettings(appState);

    // Create Modern Resources
    appState->hFontUi = CreateModernFont(10, false);
    // Use the new darker background color for the main window brush
    appState->hBrushBg = CreateSolidBrush(CLR_BG_DARK);

    const wchar_t CLASS_NAME[] = L"TrayCaddy";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = appState->hBrushBg;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    appState->mainWindow = CreateWindow(CLASS_NAME, L"TrayCaddy",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 400,
        NULL, NULL, hInstance, NULL);

    if (!appState->mainWindow) {
        if (hMutex) CloseHandle(hMutex);
        delete appState;
        return 1;
    }

    SetWindowLongPtr(appState->mainWindow, GWLP_USERDATA, (LONG_PTR)appState);
    appState->listView = GetDlgItem(appState->mainWindow, ID_LIST_WINDOWS);
    appState->hkControl = GetDlgItem(appState->mainWindow, ID_HK_CONTROL);

    MakeCustomHotkeyControl(appState->hkControl, appState->hkModifiers, appState->hkKey);

    SendMessage(appState->mainWindow, WM_SETFONT, (WPARAM)appState->hFontUi, TRUE);

    InitTrayIcon(appState->mainWindow, hInstance, &appState->mainIcon);
    InitTrayMenu(&appState->trayMenu);
    UpdateAppHotkey(appState);
    LoadState(appState);

    ShowWindow(appState->mainWindow, SW_SHOW);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RestoreAll(appState);
    Shell_NotifyIcon(NIM_DELETE, &appState->mainIcon);
    UnregisterHotKey(appState->mainWindow, HOTKEY_ID);
    if (appState->trayMenu) DestroyMenu(appState->trayMenu);

    // Cleanup Resources
    if (appState->hFontUi) DeleteObject(appState->hFontUi);
    if (appState->hBrushBg) DeleteObject(appState->hBrushBg);

    if (hMutex) CloseHandle(hMutex);
    delete appState;

    return (int)msg.wParam;
}