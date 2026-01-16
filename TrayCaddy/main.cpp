#include <Windows.h>
#include <windowsx.h>
#include <string>
#include <vector>

#define VK_Z_KEY 0x5A
#define TRAY_KEY VK_Z_KEY
#define MOD_KEY (MOD_WIN | MOD_SHIFT)

#define WM_ICON 0x1C0A
#define WM_OURICON 0x1C0B
#define EXIT_ID 0x99
#define SHOW_ALL_ID 0x98
#define MAXIMUM_WINDOWS 100

// Stores hidden window record.
typedef struct HIDDEN_WINDOW {
    NOTIFYICONDATA icon;
    HWND window;
} HIDDEN_WINDOW;

// Current execution context
typedef struct TRCONTEXT {
    HWND mainWindow;
    HIDDEN_WINDOW icons[MAXIMUM_WINDOWS];
    HMENU trayMenu;
    int iconIndex;
} TRCONTEXT;

HANDLE saveFile;

void save(const TRCONTEXT* context) {
    DWORD numbytes;
    if (saveFile == INVALID_HANDLE_VALUE) return;

    SetFilePointer(saveFile, 0, NULL, FILE_BEGIN);
    SetEndOfFile(saveFile);

    if (!context->iconIndex) {
        return;
    }

    for (int i = 0; i < context->iconIndex; i++)
    {
        if (context->icons[i].window) {
            std::wstring str;
            str = std::to_wstring((LONG_PTR)context->icons[i].window);
            str += L",";
            const wchar_t* handleString = str.c_str();
            WriteFile(saveFile, handleString, (DWORD)(wcslen(handleString) * sizeof(wchar_t)), &numbytes, NULL);
        }
    }
}

// FIXED: Changed parameter to WPARAM to receive full 32-bit HWND ID
void showWindow(TRCONTEXT* context, WPARAM callbackId) {
    for (int i = 0; i < context->iconIndex; i++)
    {
        // FIXED: Compare directly against the ID passed in wParam
        if (context->icons[i].icon.uID == (UINT)callbackId) {
            ShowWindow(context->icons[i].window, SW_SHOW);
            Shell_NotifyIcon(NIM_DELETE, &context->icons[i].icon);
            SetForegroundWindow(context->icons[i].window);
            context->icons[i] = {};

            std::vector<HIDDEN_WINDOW> temp(context->iconIndex);
            int x = 0;
            for (int j = 0; j < context->iconIndex; j++)
            {
                if (context->icons[j].window) {
                    temp[x++] = context->icons[j];
                }
            }

            if (x > 0) {
                memcpy_s(context->icons, sizeof(context->icons), temp.data(), sizeof(HIDDEN_WINDOW) * x);
            }
            for (int j = x; j < context->iconIndex; j++) {
                context->icons[j] = {};
            }
            context->iconIndex = x;
            save(context);
            break;
        }
    }
}

void minimizeToTray(TRCONTEXT* context, LONG_PTR restoreWindow) {
    const wchar_t restrictWins[][14] = { {L"WorkerW"}, {L"Shell_TrayWnd"} };

    HWND currWin = 0;
    if (!restoreWindow) {
        currWin = GetForegroundWindow();
    }
    else {
        currWin = (HWND)restoreWindow;
    }

    if (!currWin) {
        return;
    }

    wchar_t className[256];
    if (!GetClassName(currWin, className, 256)) {
        return;
    }

    for (int i = 0; i < sizeof(restrictWins) / sizeof(*restrictWins); i++)
    {
        if (wcscmp(restrictWins[i], className) == 0) {
            return;
        }
    }

    if (context->iconIndex == MAXIMUM_WINDOWS) {
        MessageBox(NULL, L"Error! Too many hidden windows. Please unhide some.", L"TrayCaddy", MB_OK | MB_ICONERROR);
        return;
    }

    ULONG_PTR icon = GetClassLongPtr(currWin, GCLP_HICONSM);
    if (!icon) {
        icon = SendMessage(currWin, WM_GETICON, 2, NULL);
        if (!icon) {
            return;
        }
    }

    NOTIFYICONDATA nid;
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = context->mainWindow;
    nid.hIcon = (HICON)icon;

    // FIXED: Removed NIF_SHOWTIP (Vista+) to ensure legacy compatibility
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

    // FIXED: Removed uVersion assignment. We want legacy behavior for these icons
    // so that wParam returns the full 32-bit HWND.
    // nid.uVersion = NOTIFYICON_VERSION_4; 

    nid.uID = (UINT)(UINT_PTR)currWin;
    nid.uCallbackMessage = WM_ICON;
    GetWindowText(currWin, nid.szTip, 128);

    context->icons[context->iconIndex].icon = nid;
    context->icons[context->iconIndex].window = currWin;
    context->iconIndex++;

    Shell_NotifyIcon(NIM_ADD, &nid);
    // FIXED: Do not set version for these icons
    // Shell_NotifyIcon(NIM_SETVERSION, &nid);

    ShowWindow(currWin, SW_HIDE);

    if (!restoreWindow) {
        save(context);
    }
}

void createTrayIcon(HWND mainWindow, HINSTANCE hInstance, NOTIFYICONDATA* icon) {
    icon->cbSize = sizeof(NOTIFYICONDATA);
    icon->hWnd = mainWindow;
    icon->hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    icon->uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
    icon->uVersion = NOTIFYICON_VERSION_4;
    icon->uID = (UINT)(UINT_PTR)mainWindow;
    icon->uCallbackMessage = WM_OURICON;
    wcscpy_s(icon->szTip, L"TrayCaddy");
    Shell_NotifyIcon(NIM_ADD, icon);
    Shell_NotifyIcon(NIM_SETVERSION, icon);
}

void createTrayMenu(HMENU* trayMenu) {
    *trayMenu = CreatePopupMenu();

    MENUITEMINFO showAllMenuItem = { sizeof(MENUITEMINFO) };
    MENUITEMINFO exitMenuItem = { sizeof(MENUITEMINFO) };

    exitMenuItem.fMask = MIIM_STRING | MIIM_ID;
    exitMenuItem.fType = MFT_STRING;
    exitMenuItem.dwTypeData = (LPWSTR)L"Exit";
    exitMenuItem.cch = 5;
    exitMenuItem.wID = EXIT_ID;

    showAllMenuItem.fMask = MIIM_STRING | MIIM_ID;
    showAllMenuItem.fType = MFT_STRING;
    showAllMenuItem.dwTypeData = (LPWSTR)L"Restore all windows";
    showAllMenuItem.cch = 20;
    showAllMenuItem.wID = SHOW_ALL_ID;

    InsertMenuItem(*trayMenu, 0, FALSE, &showAllMenuItem);
    InsertMenuItem(*trayMenu, 0, FALSE, &exitMenuItem);
}

void showAllWindows(TRCONTEXT* context) {
    for (int i = 0; i < context->iconIndex; i++)
    {
        ShowWindow(context->icons[i].window, SW_SHOW);
        Shell_NotifyIcon(NIM_DELETE, &context->icons[i].icon);
        context->icons[i] = {};
    }
    save(context);
    context->iconIndex = 0;
}

void exitApp() {
    PostQuitMessage(0);
}

void startup(TRCONTEXT* context) {
    saveFile = CreateFile(L"TrayCaddy.dat", GENERIC_READ | GENERIC_WRITE, \
        0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (saveFile == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, L"Error! TrayCaddy could not create a save file.", L"TrayCaddy", MB_OK | MB_ICONERROR);
        exitApp();
        return;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD numbytes;
        DWORD fileSize = GetFileSize(saveFile, NULL);

        if (!fileSize) return;

        FILETIME saveFileWriteTime;
        GetFileTime(saveFile, NULL, NULL, &saveFileWriteTime);

        DWORD charCount = fileSize / sizeof(wchar_t);
        std::vector<wchar_t> contents(charCount);

        if (ReadFile(saveFile, &contents.front(), fileSize, &numbytes, NULL)) {
            wchar_t handle[32];
            int index = 0;
            for (size_t i = 0; i < charCount; i++)
            {
                if (contents[i] != L',') {
                    handle[index] = contents[i];
                    index++;
                }
                else {
                    handle[index] = L'\0';
                    index = 0;
                    try {
                        minimizeToTray(context, std::stoll(std::wstring(handle)));
                    }
                    catch (...) {}
                    memset(handle, 0, sizeof(handle));
                }
            }
            if (context->iconIndex > 0) {
                std::wstring restore_message = L"TrayCaddy had previously been terminated unexpectedly.\n\nRestored " + \
                    std::to_wstring(context->iconIndex) + (context->iconIndex > 1 ? L" icons." : L" icon.");
                MessageBox(NULL, restore_message.c_str(), L"TrayCaddy", MB_OK);
            }
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    TRCONTEXT* context = (TRCONTEXT*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    POINT pt;
    switch (uMsg)
    {
    case WM_ICON:
        // FIXED: Legacy Mode Handling
        // lParam holds the mouse message (e.g., WM_LBUTTONDBLCLK)
        // wParam holds the ID (The HWND of the minimized window)
        if (lParam == WM_LBUTTONDBLCLK) {
            if (context) showWindow(context, wParam);
        }
        break;
    case WM_OURICON:
        // Main App Icon uses Version 4 (as set in createTrayIcon)
        // LOWORD(lParam) holds the mouse message
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            SetForegroundWindow(hwnd);
            GetCursorPos(&pt);
            if (context) {
                TrackPopupMenuEx(context->trayMenu, \
                    (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_BOTTOMALIGN, \
                    pt.x, pt.y, hwnd, NULL);
            }
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == 0) {
            switch (LOWORD(wParam)) {
            case SHOW_ALL_ID:
                if (context) showAllWindows(context);
                break;
            case EXIT_ID:
                exitApp();
                break;
            }
        }
        break;
    case WM_HOTKEY:
        if (context) minimizeToTray(context, NULL);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

#pragma warning( push )
#pragma warning( disable : 4100 ) 
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
#pragma warning( pop )

    TRCONTEXT* context = new TRCONTEXT;
    ZeroMemory(context, sizeof(TRCONTEXT));

    NOTIFYICONDATA icon = {};

    const wchar_t szUniqueNamedMutex[] = L"TrayCaddy_mutex";
    HANDLE mutex = CreateMutex(NULL, TRUE, szUniqueNamedMutex);

    if (mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBox(NULL, L"Error! Another instance of TrayCaddy is already running.", L"TrayCaddy", MB_OK | MB_ICONERROR);
        if (mutex) CloseHandle(mutex);
        delete context;
        return 1;
    }

    BOOL bRet;
    MSG msg;

    const wchar_t CLASS_NAME[] = L"TrayCaddy";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc)) {
        return 1;
    }

    context->mainWindow = CreateWindow(CLASS_NAME, NULL, NULL, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    if (!context->mainWindow) {
        return 1;
    }

    SetWindowLongPtr(context->mainWindow, GWLP_USERDATA, (LONG_PTR)context);

    if (!RegisterHotKey(context->mainWindow, 0, MOD_KEY | MOD_NOREPEAT, TRAY_KEY)) {
        MessageBox(NULL, L"Error! Could not register the hotkey.", L"TrayCaddy", MB_OK | MB_ICONERROR);
        return 1;
    }

    createTrayIcon(context->mainWindow, hInstance, &icon);
    createTrayMenu(&context->trayMenu);
    startup(context);

    while ((bRet = GetMessage(&msg, 0, 0, 0)) != 0)
    {
        if (bRet != -1) {
            DispatchMessage(&msg);
        }
    }

    showAllWindows(context);
    Shell_NotifyIcon(NIM_DELETE, &icon);

    // Cleanup
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    if (saveFile != INVALID_HANDLE_VALUE) CloseHandle(saveFile);
    DestroyMenu(context->trayMenu);
    DestroyWindow(context->mainWindow);
    DeleteFile(L"TrayCaddy.dat");
    UnregisterHotKey(context->mainWindow, 0);

    delete context;
    return (int)msg.wParam;
}