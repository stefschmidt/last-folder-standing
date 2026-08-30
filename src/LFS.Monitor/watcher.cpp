#include "watcher.h"

#include <algorithm>

#include "app_paths.h"
#include "mru_reader.h"

namespace lfs {
namespace {

constexpr DWORD kDirBufferBytes = 16 * 1024;

}  // namespace

bool ChangeWatcher::DirWatch::Arm() {
    if (dir == INVALID_HANDLE_VALUE) return false;
    ::ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = event;
    return ::ReadDirectoryChangesW(dir, buffer.data(), static_cast<DWORD>(buffer.size()),
                                   /*bWatchSubtree=*/FALSE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                   nullptr, &overlapped, nullptr) != FALSE;
}

bool ChangeWatcher::DirWatch::Consume() {
    DWORD transferred = 0;
    const BOOL ok = ::GetOverlappedResult(dir, &overlapped, &transferred, FALSE);
    ::ResetEvent(event);

    bool relevant = true;
    if (!onlyFile.empty()) {
        // A buffer overflow (transferred == 0) loses the names; assume relevant.
        relevant = !ok || transferred == 0;
        DWORD offset = 0;
        while (ok && transferred > 0 && offset + sizeof(FILE_NOTIFY_INFORMATION) <= transferred) {
            const auto* info =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            const size_t chars = info->FileNameLength / sizeof(WCHAR);
            const std::wstring name(info->FileName, chars);
            if (::CompareStringOrdinal(name.c_str(), static_cast<int>(name.size()),
                                       onlyFile.c_str(), static_cast<int>(onlyFile.size()),
                                       TRUE) == CSTR_EQUAL) {
                relevant = true;
                break;
            }
            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
        }
    }

    Arm();
    return relevant;
}

void ChangeWatcher::DirWatch::Close() {
    if (dir != INVALID_HANDLE_VALUE) {
        ::CancelIoEx(dir, &overlapped);
        ::CloseHandle(dir);
        dir = INVALID_HANDLE_VALUE;
    }
    if (event) {
        ::CloseHandle(event);
        event = nullptr;
    }
}

ChangeWatcher::~ChangeWatcher() { Close(); }

bool ChangeWatcher::ArmRegistry() {
    if (!regKey_ || !regEvent_) return false;
    // THREAD_AGNOSTIC keeps the registration alive independently of the thread
    // that armed it.
    return ::RegNotifyChangeKeyValue(regKey_, /*bWatchSubtree=*/TRUE,
                                     REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET |
                                         REG_NOTIFY_THREAD_AGNOSTIC,
                                     regEvent_, TRUE) == ERROR_SUCCESS;
}

bool ChangeWatcher::Start() {
    quitEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    refreshEvent_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!quitEvent_ || !refreshEvent_) return false;

    // Registry: the whole ComDlg32 subtree.
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kComDlg32Key, 0, KEY_NOTIFY, &regKey_) ==
        ERROR_SUCCESS) {
        regEvent_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!regEvent_ || !ArmRegistry()) {
            warnings_.push_back(L"Cannot watch ComDlg32 registry key - MRU updates will be missed");
            if (regEvent_) {
                ::CloseHandle(regEvent_);
                regEvent_ = nullptr;
            }
        }
    } else {
        warnings_.push_back(L"ComDlg32 registry key not found - no dialog history on this system");
    }

    const auto openDir = [this](DirWatch& w, const std::wstring& path, const wchar_t* what) {
        if (path.empty()) return;
        w.path = path;
        w.buffer.resize(kDirBufferBytes);
        w.dir = ::CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                              nullptr);
        if (w.dir == INVALID_HANDLE_VALUE) {
            warnings_.push_back(std::wstring(what) + L" not available: " + path);
            return;
        }
        w.event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!w.event || !w.Arm()) {
            warnings_.push_back(std::wstring(what) + L" cannot be watched: " + path);
            w.Close();
        }
    };

    openDir(recent_, RecentDir(), L"Recent folder");
    data_.onlyFile = L"settings.json";
    openDir(data_, DataDir(), L"Settings folder");

    // Only a total loss of all sources is fatal.
    return regEvent_ != nullptr || recent_.dir != INVALID_HANDLE_VALUE;
}

void ChangeWatcher::Run(const std::function<void(ChangeKind)>& onChange) {
    HANDLE handles[5]{};
    ChangeKind kinds[5]{};
    DWORD count = 0;

    const auto add = [&](HANDLE h, ChangeKind k) {
        if (!h) return;
        handles[count] = h;
        kinds[count] = k;
        ++count;
    };
    add(quitEvent_, ChangeKind::Manual);
    add(refreshEvent_, ChangeKind::Manual);
    add(regEvent_, ChangeKind::Sources);
    if (recent_.dir != INVALID_HANDLE_VALUE) add(recent_.event, ChangeKind::Sources);
    if (data_.dir != INVALID_HANDLE_VALUE) add(data_.event, ChangeKind::Settings);

    bool pending = false;
    ChangeKind pendingKind = ChangeKind::Sources;
    ULONGLONG firstEventTick = 0;

    for (;;) {
        DWORD timeout = INFINITE;
        if (pending) {
            const ULONGLONG elapsed = ::GetTickCount64() - firstEventTick;
            timeout = elapsed >= kMaxDebounceMs ? 0 : kDebounceMs;
        }

        const DWORD wait = ::WaitForMultipleObjects(count, handles, FALSE, timeout);

        if (wait == WAIT_TIMEOUT) {
            if (pending) {
                pending = false;
                onChange(pendingKind);
            }
            continue;
        }
        if (wait < WAIT_OBJECT_0 || wait >= WAIT_OBJECT_0 + count) return;  // failed wait

        const DWORD index = wait - WAIT_OBJECT_0;
        HANDLE signaled = handles[index];

        if (signaled == quitEvent_) {
            return;
        }

        if (signaled == regEvent_) {
            ArmRegistry();  // re-arm; a failure here just means no further events
        } else if (recent_.dir != INVALID_HANDLE_VALUE && signaled == recent_.event) {
            recent_.Consume();  // every .lnk change matters, no filtering needed
        } else if (data_.dir != INVALID_HANDLE_VALUE && signaled == data_.event) {
            if (!data_.Consume()) continue;  // our own state/snapshot write
        }

        if (!pending) {
            pending = true;
            pendingKind = kinds[index];
            firstEventTick = ::GetTickCount64();
        } else if (kinds[index] == ChangeKind::Settings) {
            pendingKind = ChangeKind::Settings;  // settings win, they need a reload
        }
    }
}

void ChangeWatcher::Stop() {
    if (quitEvent_) ::SetEvent(quitEvent_);
}

void ChangeWatcher::RequestRefresh() {
    if (refreshEvent_) ::SetEvent(refreshEvent_);
}

void ChangeWatcher::Close() {
    recent_.Close();
    data_.Close();
    if (regKey_) {
        ::RegCloseKey(regKey_);
        regKey_ = nullptr;
    }
    for (HANDLE* h : {&regEvent_, &quitEvent_, &refreshEvent_}) {
        if (*h) {
            ::CloseHandle(*h);
            *h = nullptr;
        }
    }
}

}  // namespace lfs
