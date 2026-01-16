#ifndef UNICODE
#define UNICODE
#endif

// --- Enable Visual Styles (Modern Look) ---
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <Windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h> 
#include <ShellScalingApi.h> // For DPI Awareness
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

// Link necessary libraries
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib") 
#pragma comment(lib, "Shcore.lib") // Required for DPI scaling

// --- Constants ---
#define WM_ICON     0x1C0A
#define WM_OURICON  0x1C0B
#define HOTKEY_ID   1

// Control IDs
#define ID_BTN_RESTORE_ALL  0x200
#define ID_BTN_EXIT         0x201
#define ID_LIST_WINDOWS     0x202
#define ID_HK_CONTROL       0x203
#define ID_BTN_APPLY_HK     0x205
#define ID_GRP_SETTINGS     0x206 // New Group Box ID

#define ID_MENU_RESTORE_ALL 0x98
#define ID_MENU_EXIT        0x99

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
    HFONT hFont = nullptr;

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
HFONT GetModernFont();

// --- Custom Hotkey Control Implementation ---

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

HFONT GetModernFont() {
    NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
    // Create a slightly larger font for better readability if desired, or keep default
    return CreateFontIndirect(&ncm.lfMessageFont);
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
        const int margin = 15;
        const int winWidth = 400;  // Slightly wider
        const int rowHeight = 25;
        const int btnHeight = 30;

        int currentY = margin;

        // 1. LIST VIEW (Full width minus margins)
        int listHeight = 180;
        HWND hList = CreateWindow(WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            margin, currentY, winWidth - (margin * 2), listHeight,
            hwnd, (HMENU)ID_LIST_WINDOWS, GetModuleHandle(NULL), NULL);

        LVCOLUMN lvc;
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = winWidth - (margin * 2) - 4; // Adjust for scrollbar
        lvc.pszText = (LPWSTR)L"Window Title";
        ListView_InsertColumn(hList, 0, &lvc);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

        currentY += listHeight + margin;

        // 2. SETTINGS GROUP BOX
        int grpHeight = 65;
        CreateWindow(L"BUTTON", L"Settings",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            margin, currentY, winWidth - (margin * 2), grpHeight,
            hwnd, (HMENU)ID_GRP_SETTINGS, GetModuleHandle(NULL), NULL);

        // Coordinates inside the group box context
        int grpInnerY = currentY + 25;
        int grpInnerX = margin + 15;
        int grpContentWidth = winWidth - (margin * 2) - 30;

        // Label: "Trigger Hotkey:"
        CreateWindow(L"STATIC", L"Tray Hotkey:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            grpInnerX, grpInnerY, 80, rowHeight, hwnd, NULL, GetModuleHandle(NULL), NULL);

        // Hotkey Edit Box
        CreateWindow(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_READONLY,
            grpInnerX + 85, grpInnerY, 190, rowHeight,
            hwnd, (HMENU)ID_HK_CONTROL, GetModuleHandle(NULL), NULL);

        // Apply Button
        CreateWindow(L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            grpInnerX + 85 + 190 + 10, grpInnerY, 60, rowHeight,
            hwnd, (HMENU)ID_BTN_APPLY_HK, GetModuleHandle(NULL), NULL);

        currentY += grpHeight + margin;

        // 3. ACTION BUTTONS (Bottom aligned)
        int btnWidth = 110;
        CreateWindow(L"BUTTON", L"Restore All",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            margin, currentY, btnWidth, btnHeight,
            hwnd, (HMENU)ID_BTN_RESTORE_ALL, GetModuleHandle(NULL), NULL);

        CreateWindow(L"BUTTON", L"Exit",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            winWidth - btnWidth - margin, currentY, btnWidth, btnHeight,
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

        // --- Paint Handling for Polish ---
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        // Make text labels transparent so they don't have a gray box background
        // on the white window.
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    // --- ENABLE HIGH DPI AWARENESS (Critical for modern screens) ---
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE hMutex = CreateMutex(NULL, TRUE, L"TrayCaddy_Unique_Mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    APP_STATE* appState = new APP_STATE();
    LoadSettings(appState);
    appState->hFont = GetModernFont();

    const wchar_t CLASS_NAME[] = L"TrayCaddy";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // Calculate window size (Wider and Taller)
    int width = 430; // 400 content + borders
    int height = 360;

    appState->mainWindow = CreateWindow(CLASS_NAME, L"TrayCaddy",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
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
    SendMessage(appState->mainWindow, WM_SETFONT, (WPARAM)appState->hFont, TRUE);

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
    if (appState->hFont) DeleteObject(appState->hFont);
    if (hMutex) CloseHandle(hMutex);
    delete appState;

    return (int)msg.wParam;
}