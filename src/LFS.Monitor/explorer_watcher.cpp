#include "explorer_watcher.h"

#include <windows.h>
#include <objbase.h>
// exdisp.h needs the `interface` macro from objbase.h -- keep this order.
#include <exdisp.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <set>

#include "app_paths.h"
#include "common/json.h"
#include "common/strings.h"
#include "common/timeutil.h"

namespace lfs {
namespace {

constexpr UINT WM_SHELL_NOTIFY = WM_APP + 10;
constexpr UINT_PTR kPollTimerId = 1;
constexpr wchar_t kWindowClass[] = L"LFS_ExplorerWatcher";

const std::wstring& ActivityPath() {
    static const std::wstring p =
        DataDir().empty() ? std::wstring{} : DataDir() + L"\\explorer_activity.json";
    return p;
}

std::wstring PathOfPidl(PCIDLIST_ABSOLUTE pidl) {
    if (!pidl) return {};
    std::vector<wchar_t> buffer(32768, L'\0');
    if (!::SHGetPathFromIDListEx(pidl, buffer.data(), static_cast<DWORD>(buffer.size()),
                                 GPFIDL_DEFAULT)) {
        return {};
    }
    return std::wstring(buffer.data());
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

}  // namespace

std::vector<RawEntry> ReadExplorerActivity() {
    std::vector<RawEntry> out;

    std::string bytes;
    if (!ReadFileBytes(ActivityPath(), bytes)) return out;

    const auto doc = json::Parse(Utf8ToWide(bytes));
    if (!doc || !doc->IsObject()) return out;
    const json::Value* list = doc->Find(L"folders");
    if (!list || !list->IsArray()) return out;

    for (size_t i = 0; i < list->size(); ++i) {
        const json::Value* item = list->At(i);
        if (!item || !item->IsObject()) continue;
        const json::Value* path = item->Find(L"path");
        const json::Value* used = item->Find(L"lastUsedUtc");
        if (!path || !path->IsString() || !used || !used->IsString()) continue;

        const uint64_t ts = FromIso8601(used->AsString());
        if (ts == 0) continue;

        RawEntry e;
        e.path = path->AsString();
        e.source = Source::Explorer;
        e.rank = static_cast<int>(i);
        e.isHead = (i == 0);
        e.observedUtc = ts;
        e.group = L"explorer";
        out.push_back(std::move(e));
    }
    return out;
}

ExplorerWatcher::~ExplorerWatcher() { Stop(); }

LRESULT CALLBACK ExplorerWatcher::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ExplorerWatcher*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->Handle(hwnd, msg, wParam, lParam);
}

LRESULT ExplorerWatcher::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SHELL_NOTIFY: {
            PIDLIST_ABSOLUTE* pidls = nullptr;
            LONG event = 0;
            HANDLE lock = ::SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wParam),
                                                      static_cast<DWORD>(lParam), &pidls, &event);
            if (lock) {
                OnShellEvent(event, pidls ? pidls[0] : nullptr, pidls ? pidls[1] : nullptr);
                ::SHChangeNotification_Unlock(lock);
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == kPollTimerId) {
                PollWindows();
                return 0;
            }
            break;

        case WM_CLOSE:
            ::PostQuitMessage(0);
            return 0;

        default: break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ExplorerWatcher::Start(std::function<void()> onActivity) {
    onActivity_ = std::move(onActivity);
    Load();

    HANDLE ready = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return false;

    thread_ = std::thread([this, ready] {
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &ExplorerWatcher::WndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        ::RegisterClassExW(&wc);

        hwnd_ = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  wc.hInstance, this);
        ::SetEvent(ready);
        if (hwnd_) Run();

        ::CoUninitialize();
    });

    ::WaitForSingleObject(ready, 5000);
    ::CloseHandle(ready);
    return hwnd_ != nullptr;
}

void ExplorerWatcher::Run() {
    PIDLIST_ABSOLUTE desktop = nullptr;
    ::SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &desktop);

    SHChangeNotifyEntry entry{};
    entry.pidl = desktop;
    entry.fRecursive = TRUE;
    // Only "something appeared or moved here" events; the rest is pure noise.
    registration_ = ::SHChangeNotifyRegister(
        hwnd_, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
        SHCNE_CREATE | SHCNE_MKDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER, WM_SHELL_NOTIFY, 1,
        &entry);

    ::SetTimer(hwnd_, kPollTimerId, kWindowPollMs, nullptr);
    PollWindows();

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ::KillTimer(hwnd_, kPollTimerId);
    if (registration_) {
        ::SHChangeNotifyDeregister(registration_);
        registration_ = 0;
    }
    if (desktop) ::CoTaskMemFree(desktop);
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void ExplorerWatcher::Stop() {
    if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
}

// Which folders are shown in Explorer windows right now. Documented automation
// interface; virtual locations (This PC, search results, Libraries) do not turn
// into a filesystem path and drop out here.
void ExplorerWatcher::PollWindows() {
    IShellWindows* windows = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_IShellWindows,
                                  reinterpret_cast<void**>(&windows)))) {
        return;
    }

    std::set<std::wstring, PathLess> current;
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index;
        ::VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;

        IDispatch* dispatch = nullptr;
        const HRESULT hr = windows->Item(index, &dispatch);
        ::VariantClear(&index);
        if (FAILED(hr) || !dispatch) continue;

        IWebBrowserApp* browser = nullptr;
        if (SUCCEEDED(dispatch->QueryInterface(IID_IWebBrowserApp,
                                               reinterpret_cast<void**>(&browser))) &&
            browser) {
            BSTR url = nullptr;
            if (SUCCEEDED(browser->get_LocationURL(&url)) && url) {
                wchar_t path[MAX_PATH * 4]{};
                DWORD length = ARRAYSIZE(path);
                if (SUCCEEDED(::PathCreateFromUrlW(url, path, &length, 0)) && path[0] &&
                    IsDirectory(path)) {
                    current.insert(path);
                }
                ::SysFreeString(url);
            }
            browser->Release();
        }
        dispatch->Release();
    }
    windows->Release();

    const ULONGLONG now = ::GetTickCount64();
    std::vector<std::wstring> dwelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Drop windows that are gone, so reopening a folder counts again.
        for (auto it = open_.begin(); it != open_.end();) {
            it = current.count(it->first) == 0 ? open_.erase(it) : std::next(it);
        }

        for (const auto& folder : current) {
            auto [it, inserted] = open_.try_emplace(folder, OpenWindow{now, false});
            if (inserted) continue;
            if (!it->second.counted && now - it->second.since >= kDwellMs) {
                it->second.counted = true;
                dwelled.push_back(folder);
            }
        }
    }

    for (const auto& folder : dwelled) Record(folder);
}

void ExplorerWatcher::OnShellEvent(LONG event, PCIDLIST_ABSOLUTE first,
                                   PCIDLIST_ABSOLUTE second) {
    std::wstring path = PathOfPidl(first);
    if (event & (SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER)) {
        // On a rename or move the second PIDL is the destination, which is the
        // folder the user actually worked in.
        const std::wstring target = PathOfPidl(second);
        if (!target.empty()) path = target;
    }
    if (path.empty()) return;

    const std::wstring folder = IsDirectory(path) ? path : ParentOf(path);
    if (folder.empty()) return;

    // The filter that makes this usable: only changes inside a folder the user
    // has open in Explorer count as the user's own doing.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_.find(folder);
        if (it == open_.end()) return;
        it->second.counted = true;  // no need to also wait out the dwell time
    }

    Record(folder);
}

void ExplorerWatcher::Record(const std::wstring& folder) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activity_[folder] = NowUtc();
    }
    Save();
    if (onActivity_) onActivity_();
}

void ExplorerWatcher::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    activity_.clear();
    for (const RawEntry& e : ReadExplorerActivity()) {
        activity_[e.path] = e.observedUtc;
    }
}

void ExplorerWatcher::Save() {
    std::vector<std::pair<std::wstring, uint64_t>> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries.assign(activity_.begin(), activity_.end());
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (entries.size() > kMaxActivityEntries) entries.resize(kMaxActivityEntries);

    json::Value list = json::Value::MakeArray();
    for (const auto& [path, ts] : entries) {
        json::Value item = json::Value::MakeObject();
        item.Set(L"path", json::Value(path));
        item.Set(L"lastUsedUtc", json::Value(ToIso8601(ts)));
        list.Push(std::move(item));
    }

    json::Value root = json::Value::MakeObject();
    root.Set(L"version", json::Value(1));
    root.Set(L"folders", std::move(list));
    WriteFileAtomic(ActivityPath(), WideToUtf8(json::Serialize(root)));

    // Keep the in-memory copy bounded too.
    if (entries.size() == kMaxActivityEntries) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::wstring, uint64_t, PathLess> trimmed;
        for (const auto& [path, ts] : entries) trimmed.emplace(path, ts);
        activity_.swap(trimmed);
    }
}

}  // namespace lfs
