// Measures what Explorer activity we can observe without hooks.
//
// Two candidate sources for "the user did something in this folder", printed
// side by side so their noise levels can be compared on a real desktop:
//
//   [notify]  SHChangeNotifyRegister -- shell change notifications
//   [window]  IShellWindows          -- folders currently open in Explorer
//
// Development tool, never shipped. Run it, then use Explorer normally.
#include <windows.h>
#include <objbase.h>
// exdisp.h needs the `interface` macro from objbase.h, so it must come after it.
// Do not sort these two alphabetically.
#include <exdisp.h>
#include <fcntl.h>
#include <io.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace {

constexpr UINT WM_SHELL_NOTIFY = WM_APP + 1;
constexpr UINT_PTR kWindowPollTimer = 1;
constexpr UINT kWindowPollMs = 1500;

std::set<std::wstring> g_openFolders;
std::map<std::wstring, int> g_notifyCounts;

const wchar_t* EventName(LONG event) {
    switch (event) {
        case SHCNE_RENAMEITEM: return L"RENAMEITEM";
        case SHCNE_CREATE: return L"CREATE";
        case SHCNE_DELETE: return L"DELETE";
        case SHCNE_MKDIR: return L"MKDIR";
        case SHCNE_RMDIR: return L"RMDIR";
        case SHCNE_MEDIAINSERTED: return L"MEDIAINSERTED";
        case SHCNE_MEDIAREMOVED: return L"MEDIAREMOVED";
        case SHCNE_DRIVEREMOVED: return L"DRIVEREMOVED";
        case SHCNE_DRIVEADD: return L"DRIVEADD";
        case SHCNE_NETSHARE: return L"NETSHARE";
        case SHCNE_ATTRIBUTES: return L"ATTRIBUTES";
        case SHCNE_UPDATEDIR: return L"UPDATEDIR";
        case SHCNE_UPDATEITEM: return L"UPDATEITEM";
        case SHCNE_SERVERDISCONNECT: return L"SERVERDISCONNECT";
        case SHCNE_UPDATEIMAGE: return L"UPDATEIMAGE";
        case SHCNE_DRIVEADDGUI: return L"DRIVEADDGUI";
        case SHCNE_RENAMEFOLDER: return L"RENAMEFOLDER";
        case SHCNE_FREESPACE: return L"FREESPACE";
        case SHCNE_ASSOCCHANGED: return L"ASSOCCHANGED";
        case SHCNE_EXTENDED_EVENT: return L"EXTENDED_EVENT";
        default: return L"?";
    }
}

std::wstring PathOf(PCIDLIST_ABSOLUTE pidl) {
    if (!pidl) return {};
    wchar_t buffer[MAX_PATH * 4]{};
    if (!::SHGetPathFromIDListEx(pidl, buffer, ARRAYSIZE(buffer), GPFIDL_DEFAULT)) return {};
    return buffer;
}

std::wstring ParentOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash < 2) return {};
    if (slash == 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, slash);
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void ReportNotification(LONG event, PCIDLIST_ABSOLUTE first, PCIDLIST_ABSOLUTE second) {
    // Only events that mean "something appeared or moved" are interesting here.
    const bool relevant = (event & (SHCNE_CREATE | SHCNE_MKDIR | SHCNE_RENAMEITEM |
                                    SHCNE_RENAMEFOLDER | SHCNE_UPDATEDIR)) != 0;
    if (!relevant) return;

    std::wstring path = PathOf(first);
    if (event & (SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER)) {
        // For renames the second PIDL is the destination, which is the folder
        // the user actually worked in.
        const std::wstring target = PathOf(second);
        if (!target.empty()) path = target;
    }
    if (path.empty()) return;

    const std::wstring folder = IsDirectory(path) ? path : ParentOf(path);
    if (folder.empty()) return;

    const int seen = ++g_notifyCounts[folder];
    const bool openInExplorer = g_openFolders.count(folder) != 0;
    std::wprintf(L"[notify] %-14s %-5s x%-3d %s\n", EventName(event),
                 openInExplorer ? L"OPEN" : L"", seen, folder.c_str());
    std::fflush(stdout);
}

// Asks the shell which folders are currently shown in Explorer windows.
// Documented automation interface, no hooks, no injection.
void PollExplorerWindows() {
    IShellWindows* windows = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_IShellWindows,
                                  reinterpret_cast<void**>(&windows)))) {
        return;
    }

    std::set<std::wstring> current;
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index;
        ::VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;

        IDispatch* dispatch = nullptr;
        if (FAILED(windows->Item(index, &dispatch)) || !dispatch) {
            ::VariantClear(&index);
            continue;
        }

        IWebBrowserApp* browser = nullptr;
        if (SUCCEEDED(dispatch->QueryInterface(IID_IWebBrowserApp,
                                               reinterpret_cast<void**>(&browser))) &&
            browser) {
            BSTR url = nullptr;
            if (SUCCEEDED(browser->get_LocationURL(&url)) && url) {
                wchar_t path[MAX_PATH * 4]{};
                DWORD length = ARRAYSIZE(path);
                if (SUCCEEDED(::PathCreateFromUrlW(url, path, &length, 0)) && path[0]) {
                    if (IsDirectory(path)) current.insert(path);
                }
                ::SysFreeString(url);
            }
            browser->Release();
        }
        dispatch->Release();
        ::VariantClear(&index);
    }
    windows->Release();

    for (const auto& folder : current) {
        if (g_openFolders.count(folder) == 0) {
            std::wprintf(L"[window] opened   %s\n", folder.c_str());
        }
    }
    for (const auto& folder : g_openFolders) {
        if (current.count(folder) == 0) {
            std::wprintf(L"[window] closed   %s\n", folder.c_str());
        }
    }
    std::fflush(stdout);
    g_openFolders = current;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SHELL_NOTIFY) {
        PIDLIST_ABSOLUTE* pidls = nullptr;
        LONG event = 0;
        HANDLE lock = ::SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wParam),
                                                  static_cast<DWORD>(lParam), &pidls, &event);
        if (lock) {
            ReportNotification(event, pidls ? pidls[0] : nullptr, pidls ? pidls[1] : nullptr);
            ::SHChangeNotification_Unlock(lock);
        }
        return 0;
    }
    if (msg == WM_TIMER && wParam == kWindowPollTimer) {
        PollExplorerWindows();
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int wmain() {
    ::_setmode(_fileno(stdout), _O_U8TEXT);
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LFS_ActivityProbe";
    ::RegisterClassExW(&wc);
    const HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"probe", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                        nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;

    PIDLIST_ABSOLUTE desktop = nullptr;
    ::SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &desktop);

    SHChangeNotifyEntry entry{};
    entry.pidl = desktop;
    entry.fRecursive = TRUE;
    const ULONG registration = ::SHChangeNotifyRegister(
        hwnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery, SHCNE_ALLEVENTS,
        WM_SHELL_NOTIFY, 1, &entry);

    std::wprintf(L"Shell activity probe\n");
    std::wprintf(L"  notify registration: %s\n", registration ? L"ok" : L"FAILED");
    std::wprintf(L"  Use Explorer now: open folders, copy files, paste, drag and drop.\n");
    std::wprintf(L"  OPEN marks a folder that is open in an Explorer window right now.\n\n");
    std::fflush(stdout);

    ::SetTimer(hwnd, kWindowPollTimer, kWindowPollMs, nullptr);
    PollExplorerWindows();

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (registration) ::SHChangeNotifyDeregister(registration);
    if (desktop) ::CoTaskMemFree(desktop);
    ::CoUninitialize();
    return 0;
}
