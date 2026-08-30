#include "tray.h"

#include <shellapi.h>

namespace lfs {
namespace {

constexpr UINT WM_TRAY_CALLBACK = WM_APP + 1;
constexpr UINT WM_TRAY_UPDATE = WM_APP + 2;
constexpr UINT kIconId = 1;

constexpr UINT_PTR kCmdSettings = 100;
constexpr UINT_PTR kCmdPause = 101;
constexpr UINT_PTR kCmdQuit = 102;

constexpr wchar_t kWindowClass[] = L"LFS_MonitorTrayWindow";
constexpr wchar_t kAppName[] = L"Last Folder Standing";

// Registered by the shell when Explorer restarts; without handling it the icon
// disappears for good after every Explorer crash.
UINT TaskbarCreatedMessage() {
    static const UINT msg = ::RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}

void FillIconData(NOTIFYICONDATAW& nid, HWND hwnd) {
    ::ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kIconId;
}

}  // namespace

TrayIcon::~TrayIcon() { Destroy(); }

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TrayIcon*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->Handle(hwnd, msg, wParam, lParam);
}

LRESULT TrayIcon::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == TaskbarCreatedMessage()) {
        iconAdded_ = false;
        ApplyStatus();
        return 0;
    }

    switch (msg) {
        case WM_TRAY_CALLBACK:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
                ShowMenu();
            } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                if (onCommand_) onCommand_(TrayCommand::OpenSettings);
            }
            return 0;

        case WM_TRAY_UPDATE:
            ApplyStatus();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCmdSettings:
                    if (onCommand_) onCommand_(TrayCommand::OpenSettings);
                    return 0;
                case kCmdPause:
                    paused_ = !paused_;
                    if (onCommand_) onCommand_(TrayCommand::TogglePause);
                    ApplyStatus();
                    return 0;
                case kCmdQuit:
                    if (onCommand_) onCommand_(TrayCommand::Quit);
                    return 0;
                default: break;
            }
            break;

        case WM_CLOSE:
            if (onCommand_) onCommand_(TrayCommand::Quit);
            return 0;

        default: break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool TrayIcon::Create(const std::function<void(TrayCommand)>& onCommand) {
    onCommand_ = onCommand;
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);

    // Resource id 1 is app.ico, stamped in by CMake. Loading at the tray's own
    // size avoids the blurry look of a downscaled 32 px icon.
    icon_ = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                            ::GetSystemMetrics(SM_CXSMICON),
                                            ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayIcon::WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Never shown; a plain overlapped window is used rather than a message-only
    // one so the context menu can take the foreground.
    hwnd_ = ::CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                              nullptr, instance, this);
    if (!hwnd_) return false;

    ApplyStatus();
    return iconAdded_;
}

void TrayIcon::ApplyStatus() {
    if (!hwnd_) return;

    std::wstring folder;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        folder = topFolder_;
    }

    NOTIFYICONDATAW nid;
    FillIconData(nid, hwnd_);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    nid.hIcon = icon_ ? icon_ : ::LoadIconW(nullptr, IDI_APPLICATION);

    std::wstring tip = kAppName;
    if (paused_) {
        tip += L" (paused)";
    } else if (!folder.empty()) {
        tip += L"\n";
        tip += folder;
    }
    if (tip.size() >= ARRAYSIZE(nid.szTip)) tip.resize(ARRAYSIZE(nid.szTip) - 1);
    ::wcscpy_s(nid.szTip, tip.c_str());

    if (!iconAdded_) {
        iconAdded_ = ::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    } else {
        ::Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void TrayIcon::SetStatus(const std::wstring& topFolder, bool paused) {
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        topFolder_ = topFolder;
    }
    paused_ = paused;
    if (hwnd_) ::PostMessageW(hwnd_, WM_TRAY_UPDATE, 0, 0);
}

void TrayIcon::ShowMenu() {
    const HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    ::AppendMenuW(menu, MF_STRING, kCmdSettings, L"Open Settings");
    ::AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : MF_UNCHECKED), kCmdPause, L"Pause");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdQuit, L"Quit");

    POINT pt{};
    ::GetCursorPos(&pt);
    // Required so the menu closes when the user clicks elsewhere.
    ::SetForegroundWindow(hwnd_);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    ::PostMessageW(hwnd_, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
}

void TrayIcon::Destroy() {
    if (iconAdded_ && hwnd_) {
        NOTIFYICONDATAW nid;
        FillIconData(nid, hwnd_);
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
        iconAdded_ = false;
    }
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (icon_) {
        ::DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

}  // namespace lfs
