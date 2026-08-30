// Fourth source: folders the user works in through Explorer itself.
//
// The dialog MRU only sees Open/Save dialogs. Copying a file into a folder in
// Explorer, pasting into it or dragging something onto it leaves no trace there.
//
// Two observations are combined, both through documented APIs -- no hooks, no
// injection (docs/DEVELOPMENT.md rule 2):
//
//   IShellWindows            which folders are open in Explorer right now
//   SHChangeNotifyRegister   where files appear, get renamed or moved
//
// Neither is usable alone. Change notifications fire for every background
// process on the machine: a measurement on a normal desktop picked up
// Dropbox writing to its own cache folder within seconds of starting. So a
// change only counts when it happens in a folder that is open in Explorer.
//
// A folder qualifies when either
//   * something happens in it while it is open  -> counts immediately, or
//   * a window sits on it for kDwellMs          -> counts as "worked in".
// The dwell rule keeps folders you merely click through on the way somewhere
// else out of the list.
#pragma once

#include <windows.h>
#include <shtypes.h>  // PCIDLIST_ABSOLUTE

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "folder_entry.h"
#include "snapshot.h"  // PathLess

namespace lfs {

// How long a folder must stay open before merely looking at it counts.
inline constexpr ULONGLONG kDwellMs = 5000;
// How often the open windows are enumerated.
inline constexpr UINT kWindowPollMs = 2000;
// Upper bound on what we remember; the list is truncated to maxFolders anyway.
inline constexpr size_t kMaxActivityEntries = 50;

// Reads what the watcher recorded, as pipeline input. Safe to call when no
// watcher ever ran: the file is simply absent.
std::vector<RawEntry> ReadExplorerActivity();

class ExplorerWatcher {
public:
    ExplorerWatcher() = default;
    ~ExplorerWatcher();
    ExplorerWatcher(const ExplorerWatcher&) = delete;
    ExplorerWatcher& operator=(const ExplorerWatcher&) = delete;

    // Runs on its own thread with its own message loop, because
    // SHChangeNotifyRegister needs a window and the monitor's other watchers
    // block in WaitForMultipleObjects.
    bool Start(std::function<void()> onActivity);
    void Stop();

private:
    struct OpenWindow {
        ULONGLONG since = 0;
        bool counted = false;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void Run();
    void PollWindows();
    void OnShellEvent(LONG event, PCIDLIST_ABSOLUTE first, PCIDLIST_ABSOLUTE second);
    void Record(const std::wstring& folder);
    void Load();
    void Save();

    std::thread thread_;
    HWND hwnd_ = nullptr;
    ULONG registration_ = 0;
    std::function<void()> onActivity_;

    std::mutex mutex_;
    std::map<std::wstring, uint64_t, PathLess> activity_;  // folder -> last used (UTC)
    std::map<std::wstring, OpenWindow, PathLess> open_;    // folder -> window state
};

}  // namespace lfs
