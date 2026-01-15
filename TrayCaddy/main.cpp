#include <Windows.h>
#include <windowsx.h>
#include <string>
#include <vector>

#define VK_Z_KEY 0x5A
// These keys are used to send windows to tray
#define TRAY_KEY VK_Z_KEY
#define MOD_KEY MOD_WIN + MOD_SHIFT

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
    int iconIndex; // How many windows are currently hidden
} TRCONTEXT;

HANDLE saveFile;

// Saves our hidden windows so they can be restored in case
// of crashing.
void save(const TRCONTEXT* context) {
    DWORD numbytes;
    // Truncate file
    SetFilePointer(saveFile, 0, NULL, FILE_BEGIN);
    SetEndOfFile(saveFile);
    if (!context->iconIndex) {
        return;
    }
    for (int i = 0; i < context->iconIndex; i++)
    {
        if (context->icons[i].window) {
            std::wstring str;
            // Use to_wstring for Unicode string
            str = std::to_wstring((LONG_PTR)context->icons[i].window);
            str += L",";
            const wchar_t* handleString = str.c_str();
            // WriteFile writes bytes, so we multiply length by sizeof(wchar_t)
            WriteFile(saveFile, handleString, (DWORD)(wcslen(handleString) * sizeof(wchar_t)), &numbytes, NULL);
        }
    }
}

// Restores a window
void showWindow(TRCONTEXT* context, LPARAM lParam) {
    for (int i = 0; i < context->iconIndex; i++)
    {
        if (context->icons[i].icon.uID == HIWORD(lParam)) {
            ShowWindow(context->icons[i].window, SW_SHOW);
            Shell_NotifyIcon(NIM_DELETE, &context->icons[i].icon);
            SetForegroundWindow(context->icons[i].window);
            context->icons[i] = {};
            std::vector<HIDDEN_WINDOW> temp = std::vector<HIDDEN_WINDOW>(context->iconIndex);
            // Restructure array so there are no holes
            for (int j = 0, x = 0; j < context->iconIndex; j++)
            {
                if (context->icons[j].window) {
                    temp[x] = context->icons[j];
                    x++;
                }
            }
            // Logic remains valid for structs
            memcpy_s(context->icons, sizeof(context->icons), &temp.front(), sizeof(HIDDEN_WINDOW) * context->iconIndex);
            context->iconIndex--;
            save(context);
            break;
        }
    }
}

// Minimizes the current window to tray.
void minimizeToTray(TRCONTEXT* context, LONG_PTR restoreWindow) {
    // Taskbar and desktop windows are restricted from hiding.
    // Unicode: use wchar_t and L""
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
    else {
        for (int i = 0; i < sizeof(restrictWins) / sizeof(*restrictWins); i++)
        {
            // Unicode comparison
            if (wcscmp(restrictWins[i], className) == 0) {
                return;
            }
        }
    }
    if (context->iconIndex == MAXIMUM_WINDOWS) {
        MessageBox(NULL, L"Error! Too many hidden windows. Please unhide some.", L"Traymond", MB_OK | MB_ICONERROR);
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
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uVersion = NOTIFYICON_VERSION_4;
    nid.uID = LOWORD(reinterpret_cast<UINT>(currWin));
    nid.uCallbackMessage = WM_ICON;
    GetWindowText(currWin, nid.szTip, 128); // Standard API maps to GetWindowTextW now
    context->icons[context->iconIndex].icon = nid;
    context->icons[context->iconIndex].window = currWin;
    context->iconIndex++;
    Shell_NotifyIcon(NIM_ADD, &nid);
    Shell_NotifyIcon(NIM_SETVERSION, &nid);
    ShowWindow(currWin, SW_HIDE);
    if (!restoreWindow) {
        save(context);
    }
}

// Adds our own icon to tray
void createTrayIcon(HWND mainWindow, HINSTANCE hInstance, NOTIFYICONDATA* icon) {
    icon->cbSize = sizeof(NOTIFYICONDATA);
    icon->hWnd = mainWindow;
    icon->hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    icon->uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
    icon->uVersion = NOTIFYICON_VERSION_4;
    icon->uID = reinterpret_cast<UINT>(mainWindow);
    icon->uCallbackMessage = WM_OURICON;
    // Unicode copy
    wcscpy_s(icon->szTip, L"Traymond");
    Shell_NotifyIcon(NIM_ADD, icon);
    Shell_NotifyIcon(NIM_SETVERSION, icon);
}

// Creates our tray icon menu
void createTrayMenu(HMENU* trayMenu) {
    *trayMenu = CreatePopupMenu();

    MENUITEMINFO showAllMenuItem;
    MENUITEMINFO exitMenuItem;

    exitMenuItem.cbSize = sizeof(MENUITEMINFO);
    exitMenuItem.fMask = MIIM_STRING | MIIM_ID;
    exitMenuItem.fType = MFT_STRING;
    // Cast away constness for legacy API, pointing to L"" string
    exitMenuItem.dwTypeData = (LPWSTR)L"Exit";
    exitMenuItem.cch = 5;
    exitMenuItem.wID = EXIT_ID;

    showAllMenuItem.cbSize = sizeof(MENUITEMINFO);
    showAllMenuItem.fMask = MIIM_STRING | MIIM_ID;
    showAllMenuItem.fType = MFT_STRING;
    showAllMenuItem.dwTypeData = (LPWSTR)L"Restore all windows";
    showAllMenuItem.cch = 20;
    showAllMenuItem.wID = SHOW_ALL_ID;

    InsertMenuItem(*trayMenu, 0, FALSE, &showAllMenuItem);
    InsertMenuItem(*trayMenu, 0, FALSE, &exitMenuItem);
}

// Shows all hidden windows;
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

// Creates and reads the save file to restore hidden windows in case of unexpected termination
void startup(TRCONTEXT* context) {
    // L"traymond.dat" for Unicode filename
    if ((saveFile = CreateFile(L"traymond.dat", GENERIC_READ | GENERIC_WRITE, \
        0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, L"Error! Traymond could not create a save file.", L"Traymond", MB_OK | MB_ICONERROR);
        exitApp();
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD numbytes;
        DWORD fileSize = GetFileSize(saveFile, NULL);

        if (!fileSize) {
            return;
        };

        FILETIME saveFileWriteTime;
        GetFileTime(saveFile, NULL, NULL, &saveFileWriteTime);
        uint64_t writeTime = ((uint64_t)saveFileWriteTime.dwHighDateTime << 32 | (uint64_t)saveFileWriteTime.dwLowDateTime) / 10000;
        GetSystemTimeAsFileTime(&saveFileWriteTime);
        writeTime = (((uint64_t)saveFileWriteTime.dwHighDateTime << 32 | (uint64_t)saveFileWriteTime.dwLowDateTime) / 10000) - writeTime;

        if (GetTickCount64() < writeTime) {
            return;
        }

        // Read into wchar_t buffer (fileSize is bytes, so we divide by sizeof(wchar_t) to get count)
        // Note: This assumes the file was written as WideChars (which our save function now does)
        DWORD charCount = fileSize / sizeof(wchar_t);
        std::vector<wchar_t> contents(charCount);

        ReadFile(saveFile, &contents.front(), fileSize, &numbytes, NULL);

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
                // stoll works on wstring automatically
                minimizeToTray(context, std::stoll(std::wstring(handle)));
                memset(handle, 0, sizeof(handle));
            }
        }
        std::wstring restore_message = L"Traymond had previously been terminated unexpectedly.\n\nRestored " + \
            std::to_wstring(context->iconIndex) + (context->iconIndex > 1 ? L" icons." : L" icon.");
        MessageBox(NULL, restore_message.c_str(), L"Traymond", MB_OK);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    TRCONTEXT* context = (TRCONTEXT*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    POINT pt;
    switch (uMsg)
    {
    case WM_ICON:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            showWindow(context, lParam);
        }
        break;
    case WM_OURICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            SetForegroundWindow(hwnd);
            GetCursorPos(&pt);
            TrackPopupMenuEx(context->trayMenu, \
                (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_BOTTOMALIGN, \
                pt.x, pt.y, hwnd, NULL);
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == 0) {
            switch LOWORD(wParam) {
            case SHOW_ALL_ID:
                showAllWindows(context);
                break;
            case EXIT_ID:
                exitApp();
                break;
            }
        }
        break;
    case WM_HOTKEY:
        minimizeToTray(context, NULL);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

#pragma warning( push )
#pragma warning( disable : 4100 )
// Changed to wWinMain for Unicode Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) {
#pragma warning( pop )

    TRCONTEXT context = {};

    NOTIFYICONDATA icon = {};

    // L prefix for Unicode literal
    const wchar_t szUniqueNamedMutex[] = L"traymond_mutex";
    HANDLE mutex = CreateMutex(NULL, TRUE, szUniqueNamedMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBox(NULL, L"Error! Another instance of Traymond is already running.", L"Traymond", MB_OK | MB_ICONERROR);
        return 1;
    }

    BOOL bRet;
    MSG msg;

    const wchar_t CLASS_NAME[] = L"Traymond";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc)) {
        return 1;
    }

    // Default to HWND_MESSAGE for message-only window
    context.mainWindow = CreateWindow(CLASS_NAME, NULL, NULL, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    if (!context.mainWindow) {
        return 1;
    }

    SetWindowLongPtr(context.mainWindow, GWLP_USERDATA, (LONG_PTR)&context);

    if (!RegisterHotKey(context.mainWindow, 0, MOD_KEY | MOD_NOREPEAT, TRAY_KEY)) {
        MessageBox(NULL, L"Error! Could not register the hotkey.", L"Traymond", MB_OK | MB_ICONERROR);
        return 1;
    }

    createTrayIcon(context.mainWindow, hInstance, &icon);
    createTrayMenu(&context.trayMenu);
    startup(&context);

    while ((bRet = GetMessage(&msg, 0, 0, 0)) != 0)
    {
        if (bRet != -1) {
            DispatchMessage(&msg);
        }
    }

    showAllWindows(&context);
    Shell_NotifyIcon(NIM_DELETE, &icon);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    CloseHandle(saveFile);
    DestroyMenu(context.trayMenu);
    DestroyWindow(context.mainWindow);
    DeleteFile(L"traymond.dat");
    UnregisterHotKey(context.mainWindow, 0);
    return (int)msg.wParam;
}