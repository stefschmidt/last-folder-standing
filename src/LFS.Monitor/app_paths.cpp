#include "app_paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <vector>

#include "common/file_io.h"

namespace lfs {
namespace {

std::wstring MakeDataDir() {
    std::wstring dir = DataDirPath();
    if (dir.empty()) return {};
    if (!::CreateDirectoryW(dir.c_str(), nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    return dir;
}

}  // namespace

const std::wstring& DataDir() {
    static const std::wstring dir = MakeDataDir();
    return dir;
}

const std::wstring& StatePath() {
    static const std::wstring p = DataDir().empty() ? std::wstring{} : DataDir() + L"\\state.json";
    return p;
}

const std::wstring& SettingsPath() {
    static const std::wstring p =
        DataDir().empty() ? std::wstring{} : DataDir() + L"\\settings.json";
    return p;
}

const std::wstring& SnapshotPath() {
    static const std::wstring p =
        DataDir().empty() ? std::wstring{} : DataDir() + L"\\mru_snapshot.json";
    return p;
}

const std::wstring& RecentDir() {
    static const std::wstring p = [] {
        const std::wstring roaming = KnownFolderPath(FOLDERID_RoamingAppData);
        if (roaming.empty()) return std::wstring{};
        return roaming + L"\\Microsoft\\Windows\\Recent";
    }();
    return p;
}

const std::wstring& TempDir() {
    static const std::wstring p = [] {
        wchar_t buf[MAX_PATH + 1]{};
        const DWORD n = ::GetTempPathW(MAX_PATH, buf);
        if (n == 0 || n > MAX_PATH) return std::wstring{};
        std::wstring t(buf, n);
        while (!t.empty() && t.back() == L'\\') t.pop_back();
        return t;
    }();
    return p;
}

}  // namespace lfs
