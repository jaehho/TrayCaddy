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
#include <commctrl.h> // Required for ListView
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

// Link necessary libraries
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib") // Required for InitCommonControlsEx

// --- Constants ---
#define VK_Z_KEY 0x5A
#define TRAY_KEY VK_Z_KEY
#define MOD_KEY (MOD_WIN | MOD_SHIFT)

#define WM_ICON     0x1C0A
#define WM_OURICON  0x1C0B

// Control IDs
#define ID_BTN_RESTORE_ALL  0x200
#define ID_BTN_EXIT         0x201
#define ID_LIST_WINDOWS     0x202

#define ID_MENU_RESTORE_ALL 0x98
#define ID_MENU_EXIT        0x99

const std::wstring SAVE_FILE = L"TrayCaddy.dat";

// --- Data Structures ---
struct HIDDEN_WINDOW {
    NOTIFYICONDATA icon = { 0 };
    HWND window = nullptr;
    UINT iconId = 0;
    std::wstring title; // Store title for list view
};

struct APP_STATE {
    HWND mainWindow = nullptr;
    HWND listView = nullptr; // Handle to the list control
    HMENU trayMenu = nullptr;
    std::vector<HIDDEN_WINDOW> hiddenWindows;
    UINT nextHiddenIconId = 1000;
    NOTIFYICONDATA mainIcon = { 0 };
    HFONT hFont = nullptr; // Font handle
};

// --- Forward Declarations ---
void SaveState(const APP_STATE* state);
void RestoreWindow(APP_STATE* state, UINT iconId);
void RestoreAll(APP_STATE* state);
void MinimizeToTray(APP_STATE* state, HWND targetWindow = NULL);
void InitTrayIcon(HWND hWnd, HINSTANCE hInstance, NOTIFYICONDATA* icon);
void InitTrayMenu(HMENU* trayMenu);
void LoadState(APP_STATE* state);
void ReaddHiddenIcons(APP_STATE* state);
void UpdateListView(APP_STATE* state);
HFONT GetModernFont();

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

void ReaddHiddenIcons(APP_STATE* state) {
    if (!state) return;

    // Clean invalid windows first
    state->hiddenWindows.erase(
        std::remove_if(state->hiddenWindows.begin(), state->hiddenWindows.end(),
            [](const HIDDEN_WINDOW& item) {
                return !item.window || !IsWindow(item.window);
            }),
        state->hiddenWindows.end());

    for (auto& item : state->hiddenWindows) {
        Shell_NotifyIcon(NIM_ADD, &item.icon);
        item.icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon(NIM_SETVERSION, &item.icon);
    }
    UpdateListView(state);
}

// Helper to refresh the GUI list
void UpdateListView(APP_STATE* state) {
    if (!state->listView) return;
    ListView_DeleteAllItems(state->listView);

    LVITEM lvItem = { 0 };
    lvItem.mask = LVIF_TEXT | LVIF_PARAM;

    int index = 0;
    for (const auto& item : state->hiddenWindows) {
        lvItem.iItem = index;
        lvItem.iSubItem = 0;
        // Use the title we stored, or "Unknown"
        lvItem.pszText = const_cast<LPWSTR>(item.title.empty() ? L"Unknown Window" : item.title.c_str());
        // Store the Icon ID in lParam to find it later
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
        if (item.window && IsWindow(item.window)) {
            ShowWindow(item.window, SW_RESTORE);
        }
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
    if (currWin == state->mainWindow) return; // Don't hide self

    wchar_t className[256];
    if (GetClassName(currWin, className, 256)) {
        for (const auto& restricted : restrictWins) {
            if (wcscmp(restricted, className) == 0) return;
        }
    }

    HICON hIcon = (HICON)GetClassLongPtr(currWin, GCLP_HICONSM);
    if (!hIcon) {
        hIcon = (HICON)SendMessage(currWin, WM_GETICON, ICON_SMALL, 0);
        if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

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
        newItem.title = std::wstring(titleBuf); // Store for listview

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
    int restoredCount = 0;
    std::wstring line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            uintptr_t val = std::stoull(line);
            HWND hwnd = (HWND)val;
            if (IsWindow(hwnd)) {
                MinimizeToTray(state, hwnd);
                restoredCount++;
            }
        }
        catch (...) {}
    }
    UpdateListView(state);
}

// Create standard UI font (Segoe UI)
HFONT GetModernFont() {
    NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
    return CreateFontIndirect(&ncm.lfMessageFont);
}

// --- Window Procedure ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    APP_STATE* state = (APP_STATE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    static UINT s_taskbarCreatedMsg = 0;

    if (s_taskbarCreatedMsg == 0) s_taskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");
    if (s_taskbarCreatedMsg != 0 && uMsg == s_taskbarCreatedMsg && state) {
        InitTrayIcon(hwnd, GetModuleHandle(NULL), &state->mainIcon);
        ReaddHiddenIcons(state);
        return 0;
    }

    switch (uMsg) {
    case WM_CREATE: {
        // Initialize Common Controls
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);

        // Define modern layout dimensions
        int btnHeight = 30;
        int padding = 10;
        int listHeight = 180;
        int winWidth = 300;

        // Create List View
        HWND hList = CreateWindow(WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            padding, padding, winWidth - (padding * 2), listHeight,
            hwnd, (HMENU)ID_LIST_WINDOWS, GetModuleHandle(NULL), NULL);

        // Add single column to list view
        LVCOLUMN lvc;
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = winWidth - (padding * 2) - 4; // Adjust for scrollbar
        lvc.pszText = (LPWSTR)L"Window Title";
        ListView_InsertColumn(hList, 0, &lvc);

        // Apply extended styles (full row select)
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        // Buttons
        HWND hBtnRestore = CreateWindow(L"BUTTON", L"Restore All",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            padding, listHeight + (padding * 2), 135, btnHeight,
            hwnd, (HMENU)ID_BTN_RESTORE_ALL, GetModuleHandle(NULL), NULL);

        HWND hBtnExit = CreateWindow(L"BUTTON", L"Exit App",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            winWidth - 135 - padding, listHeight + (padding * 2), 135, btnHeight,
            hwnd, (HMENU)ID_BTN_EXIT, GetModuleHandle(NULL), NULL);

        // Create state if not exists (handled in main usually, but just in case)
        if (!state) {
            // We need the state pointer to be set before controls if we want to store font immediately
            // But WM_CREATE is called before SetWindowLongPtr returns in Main.
        }
        return 0;
    }

    case WM_SETFONT: {
        // Helper to apply font to all children
        EnumChildWindows(hwnd, [](HWND child, LPARAM lparam) -> BOOL {
            SendMessage(child, WM_SETFONT, (WPARAM)lparam, TRUE);
            return TRUE;
            }, (LPARAM)state->hFont);
        return 0;
    }

    case WM_ICON:
        if (lParam == WM_LBUTTONDBLCLK && state) RestoreWindow(state, (UINT)wParam);
        break;

    case WM_OURICON:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            if (state) TrackPopupMenu(state->trayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_RESTORE_ALL || id == ID_MENU_RESTORE_ALL) {
            if (state) RestoreAll(state);
        }
        else if (id == ID_BTN_EXIT || id == ID_MENU_EXIT) {
            PostQuitMessage(0);
        }
        break;
    }

                   // Handle List View Interactions
    case WM_NOTIFY: {
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (lpnmh->idFrom == ID_LIST_WINDOWS && state) {
            if (lpnmh->code == NM_DBLCLK) {
                // Restore window on double click
                LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
                if (lpnmitem->iItem != -1) {
                    LVITEM item = { 0 };
                    item.iItem = lpnmitem->iItem;
                    item.mask = LVIF_PARAM;
                    ListView_GetItem(state->listView, &item);
                    RestoreWindow(state, (UINT)item.lParam);
                }
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
        if (state) MinimizeToTray(state, NULL);
        break;

        // Paint background White (Modern) instead of Gray
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return (LRESULT)GetStockObject(WHITE_BRUSH);

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    HANDLE hMutex = CreateMutex(NULL, TRUE, L"TrayCaddy_Unique_Mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    APP_STATE* appState = new APP_STATE();
    appState->hFont = GetModernFont(); // Load Segoe UI

    const wchar_t CLASS_NAME[] = L"TrayCaddy";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH); // Modern White Background
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // Increased size for better layout
    appState->mainWindow = CreateWindow(CLASS_NAME, L"TrayCaddy",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 316, 280,
        NULL, NULL, hInstance, NULL);

    if (!appState->mainWindow) {
        if (hMutex) CloseHandle(hMutex);
        delete appState;
        return 1;
    }

    SetWindowLongPtr(appState->mainWindow, GWLP_USERDATA, (LONG_PTR)appState);

    // Grab handle to list view
    appState->listView = GetDlgItem(appState->mainWindow, ID_LIST_WINDOWS);

    // Apply Fonts
    SendMessage(appState->mainWindow, WM_SETFONT, 0, 0);

    InitTrayIcon(appState->mainWindow, hInstance, &appState->mainIcon);
    InitTrayMenu(&appState->trayMenu);

    if (!RegisterHotKey(appState->mainWindow, 1, MOD_KEY | MOD_NOREPEAT, TRAY_KEY)) {
        MessageBox(NULL, L"Could not register Win+Shift+Z", L"Error", MB_ICONEXCLAMATION);
    }

    LoadState(appState);
    ShowWindow(appState->mainWindow, SW_SHOW);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RestoreAll(appState);
    Shell_NotifyIcon(NIM_DELETE, &appState->mainIcon);
    UnregisterHotKey(appState->mainWindow, 1);
    if (appState->trayMenu) DestroyMenu(appState->trayMenu);
    if (appState->hFont) DeleteObject(appState->hFont);
    if (hMutex) CloseHandle(hMutex);
    delete appState;

    return (int)msg.wParam;
}