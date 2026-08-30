// LFS.Settings -- the two settings, nothing else.
//
// Plain Win32 on purpose: no runtime to install, no admin, ~60 KB. The monitor
// picks up changes through its own file watcher, so there is no IPC here.
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#include "common/settings_io.h"
#include "common/strings.h"
#include "resource.h"

namespace {

constexpr wchar_t kMutexName[] = L"Local\\LastFolderStanding.Settings";
constexpr wchar_t kWindowTitle[] = L"Last Folder Standing";

lfs::Settings g_settings;

void FillList(HWND dialog) {
    HWND list = ::GetDlgItem(dialog, IDC_EXCLUDE_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& pattern : g_settings.excludePaths) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pattern.c_str()));
    }
    ::EnableWindow(::GetDlgItem(dialog, IDC_REMOVE), !g_settings.excludePaths.empty());
}

// Shows what a pattern actually resolves to, so "%TEMP%" is not a guess.
void UpdateResolved(HWND dialog) {
    HWND list = ::GetDlgItem(dialog, IDC_EXCLUDE_LIST);
    const LRESULT selection = ::SendMessageW(list, LB_GETCURSEL, 0, 0);
    std::wstring text;

    if (selection != LB_ERR && selection >= 0 &&
        static_cast<size_t>(selection) < g_settings.excludePaths.size()) {
        const std::wstring& pattern = g_settings.excludePaths[static_cast<size_t>(selection)];
        const std::wstring expanded = lfs::ExpandEnv(pattern);
        if (expanded != pattern) text = pattern + L"  \x2192  " + expanded;
    }
    ::SetDlgItemTextW(dialog, IDC_RESOLVED, text.c_str());
    ::EnableWindow(::GetDlgItem(dialog, IDC_REMOVE), selection != LB_ERR);
}

void AddPattern(HWND dialog, const std::wstring& pattern) {
    const std::wstring trimmed(lfs::TrimWs(pattern));
    if (trimmed.empty()) return;
    for (const auto& existing : g_settings.excludePaths) {
        if (lfs::IEquals(existing, trimmed)) return;  // already there
    }
    g_settings.excludePaths.push_back(trimmed);
    FillList(dialog);
    ::SendMessageW(::GetDlgItem(dialog, IDC_EXCLUDE_LIST), LB_SETCURSEL,
                   g_settings.excludePaths.size() - 1, 0);
    UpdateResolved(dialog);
}

std::wstring PickFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog)))) {
        return {};
    }

    std::wstring result;
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(L"Exclude this folder");

    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                ::CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

INT_PTR CALLBACK PatternDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::wstring* target = nullptr;

    switch (msg) {
        case WM_INITDIALOG:
            target = reinterpret_cast<std::wstring*>(lParam);
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                wchar_t buffer[1024]{};
                ::GetDlgItemTextW(dialog, IDC_PATTERN_EDIT, buffer, ARRAYSIZE(buffer));
                if (target) *target = buffer;
                ::EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                ::EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;

        default: break;
    }
    return FALSE;
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            ::SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                           reinterpret_cast<LPARAM>(::LoadIconW(nullptr, IDI_APPLICATION)));

            HWND spin = ::GetDlgItem(dialog, IDC_FOLDER_SPIN);
            ::SendMessageW(spin, UDM_SETBUDDY,
                           reinterpret_cast<WPARAM>(::GetDlgItem(dialog, IDC_FOLDER_COUNT)), 0);
            ::SendMessageW(spin, UDM_SETRANGE32, lfs::kMinFolders, lfs::kMaxFolders);
            ::SendMessageW(spin, UDM_SETPOS32, 0, g_settings.maxFolders);

            FillList(dialog);
            UpdateResolved(dialog);
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_EXCLUDE_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) UpdateResolved(dialog);
                    return TRUE;

                case IDC_ADD_FOLDER: {
                    const std::wstring folder = PickFolder(dialog);
                    if (!folder.empty()) AddPattern(dialog, folder);
                    return TRUE;
                }

                case IDC_ADD_PATTERN: {
                    std::wstring pattern;
                    if (::DialogBoxParamW(::GetModuleHandleW(nullptr),
                                          MAKEINTRESOURCEW(IDD_PATTERN), dialog,
                                          &PatternDialogProc,
                                          reinterpret_cast<LPARAM>(&pattern)) == IDOK) {
                        AddPattern(dialog, pattern);
                    }
                    return TRUE;
                }

                case IDC_REMOVE: {
                    HWND list = ::GetDlgItem(dialog, IDC_EXCLUDE_LIST);
                    const LRESULT selection = ::SendMessageW(list, LB_GETCURSEL, 0, 0);
                    if (selection != LB_ERR && selection >= 0 &&
                        static_cast<size_t>(selection) < g_settings.excludePaths.size()) {
                        g_settings.excludePaths.erase(g_settings.excludePaths.begin() + selection);
                        FillList(dialog);
                        UpdateResolved(dialog);
                    }
                    return TRUE;
                }

                case IDOK: {
                    BOOL translated = FALSE;
                    const UINT value = ::GetDlgItemInt(dialog, IDC_FOLDER_COUNT, &translated,
                                                       FALSE);
                    g_settings.maxFolders =
                        translated ? lfs::ClampFolders(static_cast<int>(value))
                                   : lfs::kDefaultFolders;

                    if (!lfs::SaveSettings(g_settings)) {
                        ::MessageBoxW(dialog,
                                      L"Could not write settings.json.\n\n"
                                      L"Check that %LOCALAPPDATA%\\LastFolderStanding is "
                                      L"writable.",
                                      kWindowTitle, MB_OK | MB_ICONWARNING);
                        return TRUE;  // keep the window open so nothing is lost
                    }
                    ::EndDialog(dialog, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    ::EndDialog(dialog, IDCANCEL);
                    return TRUE;

                default: break;
            }
            break;

        case WM_CLOSE:
            ::EndDialog(dialog, IDCANCEL);
            return TRUE;

        default: break;
    }
    return FALSE;
}

// Already running? Raise that window instead of opening a second one.
bool RaiseExistingWindow() {
    HWND existing = ::FindWindowW(L"#32770", kWindowTitle);
    if (!existing) return false;
    ::ShowWindow(existing, SW_RESTORE);
    ::SetForegroundWindow(existing);
    return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        RaiseExistingWindow();
        ::CloseHandle(mutex);
        return 0;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&controls);

    g_settings = lfs::LoadSettings();

    ::DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, &SettingsDialogProc, 0);

    ::CoUninitialize();
    if (mutex) {
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
    }
    return 0;
}
