// Watches the MRU sources and the settings file, and calls back once per
// debounced burst of changes.
//
// A single save writes several registry values in a row, so the debounce is not
// an optimization -- without it we would rebuild the list a dozen times per save.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace lfs {

// Quiet period after the last change before the pipeline runs.
inline constexpr DWORD kDebounceMs = 500;
// Upper bound, so a continuous stream of changes still produces updates.
inline constexpr DWORD kMaxDebounceMs = 3000;

enum class ChangeKind { Sources, Settings, Manual };

class ChangeWatcher {
public:
    ChangeWatcher() = default;
    ~ChangeWatcher();
    ChangeWatcher(const ChangeWatcher&) = delete;
    ChangeWatcher& operator=(const ChangeWatcher&) = delete;

    // Arms all watches. Sources that are unavailable (no Recent folder, for
    // example) are simply skipped -- that is a normal state, not an error.
    bool Start();

    // Blocks until Stop(). Calls onChange after every debounced burst.
    void Run(const std::function<void(ChangeKind)>& onChange);

    // Both are safe to call from another thread.
    void Stop();
    void RequestRefresh();

    const std::vector<std::wstring>& warnings() const { return warnings_; }

private:
    struct DirWatch {
        HANDLE dir = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED overlapped{};
        std::vector<BYTE> buffer;
        std::wstring path;
        // Only changes to this file count. Empty = every change counts.
        // We write into the data directory ourselves, so without this filter our
        // own state.json/snapshot writes would retrigger the pipeline.
        std::wstring onlyFile;

        bool Arm();
        // Consumes the completed read and reports whether it is relevant.
        bool Consume();
        void Close();
    };

    bool ArmRegistry();
    void Close();

    HKEY regKey_ = nullptr;
    HANDLE regEvent_ = nullptr;
    HANDLE quitEvent_ = nullptr;
    HANDLE refreshEvent_ = nullptr;
    DirWatch recent_;
    DirWatch data_;
    std::vector<std::wstring> warnings_;
};

}  // namespace lfs
