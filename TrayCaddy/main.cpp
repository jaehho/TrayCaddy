#ifndef UNICODE
#define UNICODE
#endif

#include <Windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

// Link necessary libraries automatically
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// --- Constants & Definitions ---
#define VK_Z_KEY 0x5A
#define TRAY_KEY VK_Z_KEY
#define MOD_KEY (MOD_WIN | MOD_SHIFT)

#define WM_ICON     0x1C0A
#define WM_OURICON  0x1C0B

#define ID_BTN_RESTORE_ALL  0x200
#define ID_BTN_EXIT         0x201
#define ID_MENU_RESTORE_ALL 0x98
#define ID_MENU_EXIT        0x99

// File name for persistence
const std::wstring SAVE_FILE = L"TrayCaddy.dat";

// --- Data Structures ---
struct HIDDEN_WINDOW {
    NOTIFYICONDATA icon;
    HWND window;
};

struct APP_STATE {
    HWND mainWindow;
    HMENU trayMenu;
    std::vector<HIDDEN_WINDOW> hiddenWindows;
};

// --- Forward Declarations ---
void SaveState(const APP_STATE* state);
void RestoreWindow(APP_STATE* state, WPARAM callbackId);
void RestoreAll(APP_STATE* state);
void MinimizeToTray(APP_STATE* state, HWND targetWindow = NULL);
void InitTrayIcon(HWND hWnd, HINSTANCE hInstance, NOTIFYICONDATA* icon);
void InitTrayMenu(HMENU* trayMenu);
void LoadState(APP_STATE* state);

// --- Implementation ---

// Save logic: Only create file if windows are hidden. Delete if empty.
void SaveState(const APP_STATE* state) {
    if (state->hiddenWindows.empty()) {
        DeleteFile(SAVE_FILE.c_str());
        return;
    }

    std::wofstream file(SAVE_FILE, std::ios::trunc);
    if (!file.is_open()) return;

    for (const auto& item : state->hiddenWindows) {
        if (item.window && IsWindow(item.window)) {
            // Cast HWND to generic integer for storage
            file << (uintptr_t)item.window << L",";
        }
    }
}

// Restore a specific window based on the Tray Icon ID (which we set to the HWND)
void RestoreWindow(APP_STATE* state, WPARAM callbackId) {
    auto it = std::find_if(state->hiddenWindows.begin(), state->hiddenWindows.end(),
        [&](const HIDDEN_WINDOW& item) {
            return item.icon.uID == (UINT)callbackId;
        });

    if (it != state->hiddenWindows.end()) {
        if (it->window && IsWindow(it->window)) {
            ShowWindow(it->window, SW_SHOW);
            SetForegroundWindow(it->window);
        }
        // Remove icon from system tray
        Shell_NotifyIcon(NIM_DELETE, const_cast<PNOTIFYICONDATA>(&it->icon));

        // Remove from our vector
        state->hiddenWindows.erase(it);

        SaveState(state);
    }
}

// Restore all currently hidden windows
void RestoreAll(APP_STATE* state) {
    for (auto& item : state->hiddenWindows) {
        if (item.window && IsWindow(item.window)) {
            ShowWindow(item.window, SW_SHOW);
        }
        Shell_NotifyIcon(NIM_DELETE, &item.icon);
    }
    state->hiddenWindows.clear();
    SaveState(state); // This will trigger DeleteFile inside SaveState
}

// Logic to hide the window and add the icon
void MinimizeToTray(APP_STATE* state, HWND targetWindow) {
    const wchar_t* restrictWins[] = { L"WorkerW", L"Shell_TrayWnd", L"Progman" };

    HWND currWin = targetWindow ? targetWindow : GetForegroundWindow();

    if (!currWin || !IsWindow(currWin)) return;

    // Check against restricted system windows
    wchar_t className[256];
    if (GetClassName(currWin, className, 256)) {
        for (const auto& restricted : restrictWins) {
            if (wcscmp(restricted, className) == 0) return;
        }
    }

    // Try to get the window icon
    HICON hIcon = (HICON)GetClassLongPtr(currWin, GCLP_HICONSM);
    if (!hIcon) {
        hIcon = (HICON)SendMessage(currWin, WM_GETICON, ICON_SMALL, 0);
        if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION); // Fallback
    }

    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = state->mainWindow;
    nid.hIcon = hIcon;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_ICON;

    // IMPORTANT: We use the Window Handle (HWND) as the ID. 
    nid.uID = (UINT)(UINT_PTR)currWin;

    GetWindowText(currWin, nid.szTip, 128);

    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        ShowWindow(currWin, SW_HIDE);

        HIDDEN_WINDOW newItem;
        newItem.icon = nid;
        newItem.window = currWin;
        state->hiddenWindows.push_back(newItem);

        // Only save if this was a manual user action (not startup restoration)
        if (!targetWindow) {
            SaveState(state);
        }
    }
}

void InitTrayIcon(HWND hWnd, HINSTANCE hInstance, NOTIFYICONDATA* icon) {
    icon->cbSize = sizeof(NOTIFYICONDATA);
    icon->hWnd = hWnd;
    // Load default app icon or system 'Application' icon
    icon->hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    if (!icon->hIcon) icon->hIcon = LoadIcon(NULL, IDI_APPLICATION);

    icon->uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
    icon->uID = (UINT)(UINT_PTR)hWnd;
    icon->uCallbackMessage = WM_OURICON;
    wcscpy_s(icon->szTip, L"TrayCaddy");

    Shell_NotifyIcon(NIM_ADD, icon);

    // Set version to 4 for the main app icon logic
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
    std::wstring content;
    std::getline(file, content); // Read entire line

    if (content.empty()) return;

    std::wstringstream ss(content);
    std::wstring segment;
    int restoredCount = 0;

    while (std::getline(ss, segment, L',')) {
        if (!segment.empty()) {
            try {
                // Convert stored string back to HWND
                uintptr_t val = std::stoull(segment);
                HWND hwnd = (HWND)val;
                if (IsWindow(hwnd)) {
                    MinimizeToTray(state, hwnd);
                    restoredCount++;
                }
            }
            catch (...) { /* Ignore malformed data */ }
        }
    }

    if (restoredCount > 0) {
        std::wstring msg = L"Restored " + std::to_wstring(restoredCount) + L" windows from previous session.";
        MessageBox(NULL, msg.c_str(), L"TrayCaddy", MB_OK | MB_ICONINFORMATION);
    }
}

// --- Main Window Procedure ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    APP_STATE* state = (APP_STATE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg) {
    case WM_CREATE:
        CreateWindow(L"BUTTON", L"Restore all", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 140, 28, hwnd, (HMENU)ID_BTN_RESTORE_ALL, GetModuleHandle(NULL), NULL);
        CreateWindow(L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 48, 140, 28, hwnd, (HMENU)ID_BTN_EXIT, GetModuleHandle(NULL), NULL);
        return 0;

    case WM_ICON:
        // Logic for the Minimized Windows
        // wParam = ID (which is the HWND), lParam = Mouse Message
        if (lParam == WM_LBUTTONDBLCLK) {
            if (state) RestoreWindow(state, wParam);
        }
        break;

    case WM_OURICON:
        // Logic for the Main App Icon
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd); // Required for menu to close properly
            if (state) {
                TrackPopupMenu(state->trayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            }
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

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE); // Minimize to tray instead of closing
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_HOTKEY:
        if (state) MinimizeToTray(state, NULL);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- Entry Point ---
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single Instance Check
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"TrayCaddy_Unique_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, L"TrayCaddy is already running.", L"Error", MB_ICONERROR);
        return 1;
    }

    // App State Initialization
    APP_STATE* appState = new APP_STATE();

    // Register Window Class
    const wchar_t CLASS_NAME[] = L"TrayCaddy";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // Create Main Window
    appState->mainWindow = CreateWindow(CLASS_NAME, L"TrayCaddy",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 180, 130,
        NULL, NULL, hInstance, NULL);

    if (!appState->mainWindow) return 1;

    // Store state pointer in Window User Data
    SetWindowLongPtr(appState->mainWindow, GWLP_USERDATA, (LONG_PTR)appState);

    // Setup Main Icon
    NOTIFYICONDATA mainIcon = { 0 };
    InitTrayIcon(appState->mainWindow, hInstance, &mainIcon);

    InitTrayMenu(&appState->trayMenu);

    if (!RegisterHotKey(appState->mainWindow, 1, MOD_KEY | MOD_NOREPEAT, TRAY_KEY)) {
        MessageBox(NULL, L"Could not register Hotkey (Win+Shift+Z)!", L"Error", MB_ICONEXCLAMATION);
    }

    LoadState(appState);
    ShowWindow(appState->mainWindow, SW_SHOW);

    // Message Loop
    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    RestoreAll(appState); // Restores windows, deletes file
    Shell_NotifyIcon(NIM_DELETE, &mainIcon);
    UnregisterHotKey(appState->mainWindow, 1);
    DestroyMenu(appState->trayMenu);

    // Release Mutex and Memory
    CloseHandle(hMutex);
    delete appState;

    return (int)msg.wParam;
}