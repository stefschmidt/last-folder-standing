// LFS.Monitor -- watches the Windows MRU sources and publishes state.json.
//
//   (no args)          tray mode: watch, write state.json
//   --console          watch and print every pipeline result, write nothing
//   --console --once   single pass, then exit
//   --dump             print every raw source entry before filtering (diagnostics)
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <objbase.h>
#include <shellapi.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "app_paths.h"
#include "common/strings.h"
#include "common/timeutil.h"
#include "mru_reader.h"
#include "pipeline.h"
#include "recent_reader.h"
#include "settings.h"
#include "snapshot.h"
#include "state_writer.h"
#include "tray.h"
#include "watcher.h"

namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\LastFolderStanding.Monitor";

struct Options {
    bool console = false;
    bool once = false;
    bool dump = false;
};

Options ParseArgs(int argc, wchar_t** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--console") o.console = true;
        else if (a == L"--once") o.once = true;
        else if (a == L"--dump") o.dump = true;
    }
    return o;
}

void DumpRaw() {
    const auto print = [](const wchar_t* title, const std::vector<lfs::RawEntry>& entries) {
        std::wprintf(L"\n=== %s (%zu)\n", title, entries.size());
        std::wstring group;
        for (const auto& e : entries) {
            if (e.group != group) {
                group = e.group;
                std::wprintf(L"  [%s]\n", group.c_str());
            }
            std::wprintf(L"    #%-3d %s  %s\n", e.rank, lfs::ToIso8601(e.observedUtc).c_str(),
                         e.path.c_str());
        }
    };
    print(L"OpenSavePidlMRU", lfs::ReadOpenSaveMru());
    print(L"LastVisitedPidlMRU", lfs::ReadLastVisitedMru());
    print(L"Recent (*.lnk)", lfs::ReadRecentFolder(30));
}

// Everything the two run modes have in common: settings, snapshot, the pipeline
// and (outside console mode) the state.json writer.
class Monitor {
public:
    explicit Monitor(bool consoleMode) : console_(consoleMode) {}

    void Init() {
        settings_ = lfs::LoadSettings();
        snapshot_.Load();
        if (!console_) writer_.LoadExisting();
    }

    void Refresh(lfs::ChangeKind kind) {
        if (kind == lfs::ChangeKind::Settings) {
            const lfs::Settings updated = lfs::LoadSettings();
            const bool changed = updated.maxFolders != settings_.maxFolders ||
                                 updated.excludePaths != settings_.excludePaths;
            settings_ = updated;
            if (console_ && changed) {
                std::wprintf(L"[settings] maxFolders=%d, %zu exclude pattern(s)\n",
                             settings_.maxFolders, settings_.excludePaths.size());
            }
        }

        if (paused_.load()) return;

        lfs::PipelineStats stats;
        folders_ = lfs::BuildFolderList(settings_, snapshot_, &stats);
        snapshot_.Save();

        if (console_) {
            PrintResult(stats);
            return;
        }

        const bool written = writer_.Write(folders_);
        if (tray_) {
            tray_->SetStatus(folders_.empty() ? std::wstring{} : folders_.front().path,
                             paused_.load());
        }
        (void)written;
    }

    void PrintResult(const lfs::PipelineStats& stats) const {
        std::wprintf(L"\n--- %s  (raw %zu, excluded %zu, missing %zu, unique %zu, max %d)\n",
                     lfs::ToLocalTimeOfDay(lfs::NowUtc()).c_str(), stats.raw,
                     stats.droppedExcluded, stats.droppedMissing, stats.unique,
                     settings_.maxFolders);
        if (folders_.empty()) {
            std::wprintf(L"    (empty)\n");
            std::fflush(stdout);
            return;
        }
        int i = 0;
        for (const auto& f : folders_) {
            std::wprintf(L"  %d. %-12s %s  %s\n", ++i, lfs::SourceName(f.source),
                         lfs::ToIso8601(f.lastUsedUtc).c_str(), f.path.c_str());
        }
        std::fflush(stdout);  // stdout is fully buffered when redirected to a file
    }

    void SetTray(lfs::TrayIcon* tray) { tray_ = tray; }
    void SetPaused(bool paused) { paused_.store(paused); }
    bool paused() const { return paused_.load(); }
    const std::vector<lfs::FolderEntry>& folders() const { return folders_; }

private:
    bool console_;
    lfs::Settings settings_;
    lfs::Snapshot snapshot_;
    lfs::StateWriter writer_;
    std::vector<lfs::FolderEntry> folders_;
    std::atomic<bool> paused_{false};
    lfs::TrayIcon* tray_ = nullptr;
};

lfs::ChangeWatcher* g_watcher = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (g_watcher) g_watcher->Stop();
        return TRUE;
    }
    return FALSE;
}

void PrintWarnings(const lfs::ChangeWatcher& watcher) {
    for (const auto& w : watcher.warnings()) {
        std::fwprintf(stderr, L"[warn] %s\n", w.c_str());
    }
}

int RunConsole(Monitor& monitor, bool once) {
    std::wprintf(L"Last Folder Standing -- monitor (console mode)\n");
    std::wprintf(L"  data dir : %s\n", lfs::DataDir().c_str());
    std::wprintf(L"  recent   : %s\n",
                 lfs::RecentDir().empty() ? L"(unavailable)" : lfs::RecentDir().c_str());

    monitor.Refresh(lfs::ChangeKind::Manual);
    if (once) return 0;

    lfs::ChangeWatcher watcher;
    if (!watcher.Start()) {
        PrintWarnings(watcher);
        std::fwprintf(stderr, L"No watchable source found.\n");
        return 1;
    }
    PrintWarnings(watcher);

    g_watcher = &watcher;
    ::SetConsoleCtrlHandler(&ConsoleCtrlHandler, TRUE);
    std::wprintf(L"\nWatching. Ctrl+C to stop.\n");
    std::fflush(stdout);

    watcher.Run([&monitor](lfs::ChangeKind kind) { monitor.Refresh(kind); });

    ::SetConsoleCtrlHandler(&ConsoleCtrlHandler, FALSE);
    g_watcher = nullptr;
    return 0;
}

void LaunchSettingsApp() {
    wchar_t exe[MAX_PATH]{};
    if (!::GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe))) return;
    std::wstring path(exe);
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    path.replace(slash + 1, std::wstring::npos, L"LFS.Settings.exe");

    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Not built yet (PLAN_03). Fall back to the file itself so the settings
        // are at least reachable.
        ::ShellExecuteW(nullptr, L"open", lfs::SettingsPath().c_str(), nullptr, nullptr, SW_SHOW);
        return;
    }
    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
}

int RunTray(Monitor& monitor) {
    lfs::ChangeWatcher watcher;
    if (!watcher.Start()) return 1;

    lfs::TrayIcon tray;
    if (!tray.Create([&](lfs::TrayCommand cmd) {
            switch (cmd) {
                case lfs::TrayCommand::OpenSettings:
                    LaunchSettingsApp();
                    break;
                case lfs::TrayCommand::TogglePause:
                    monitor.SetPaused(!monitor.paused());
                    if (!monitor.paused()) watcher.RequestRefresh();
                    break;
                case lfs::TrayCommand::Quit:
                    watcher.Stop();
                    ::PostQuitMessage(0);
                    break;
            }
        })) {
        return 1;
    }
    monitor.SetTray(&tray);

    std::thread worker([&] {
        // The pipeline creates COM objects (IShellLink for .lnk targets) and
        // notifies the shell, so this thread needs its own apartment.
        const HRESULT threadHr =
            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        monitor.Refresh(lfs::ChangeKind::Manual);
        watcher.Run([&monitor](lfs::ChangeKind kind) { monitor.Refresh(kind); });
        if (SUCCEEDED(threadHr)) ::CoUninitialize();
    });

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    watcher.Stop();
    if (worker.joinable()) worker.join();
    monitor.SetTray(nullptr);
    tray.Destroy();
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const Options opts = ParseArgs(argc, argv);

    if (opts.console || opts.dump) {
        // UTF-16 console output; CP_UTF8 alone mangles non-ASCII paths.
        ::_setmode(_fileno(stdout), _O_U8TEXT);
        ::_setmode(_fileno(stderr), _O_U8TEXT);
    } else {
        // Tray mode: one binary serves both jobs, so the console this console
        // subsystem app was given is hidden and dropped right away.
        if (HWND console = ::GetConsoleWindow()) ::ShowWindow(console, SW_HIDE);
        ::FreeConsole();
    }

    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) return 1;

    struct ComGuard {
        ~ComGuard() { ::CoUninitialize(); }
    } comGuard;

    if (lfs::DataDir().empty()) {
        std::fwprintf(stderr, L"Cannot create %%LOCALAPPDATA%%\\LastFolderStanding\n");
        return 1;
    }

    if (opts.dump) {
        DumpRaw();
        return 0;
    }

    Monitor monitor(opts.console);
    monitor.Init();

    if (opts.console) return RunConsole(monitor, opts.once);

    // Tray mode is the shipped mode and must be single-instance.
    const HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (!mutex || ::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) ::CloseHandle(mutex);
        return 0;  // another monitor owns the tray; not an error
    }

    const int rc = RunTray(monitor);
    ::ReleaseMutex(mutex);
    ::CloseHandle(mutex);
    return rc;
}
