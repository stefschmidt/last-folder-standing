#include "pipeline.h"

#include <windows.h>

#include <algorithm>
#include <map>

#include "app_paths.h"
#include "common/strings.h"
#include "common/timeutil.h"
#include "mru_reader.h"
#include "recent_reader.h"

namespace lfs {
namespace {

bool IsUnc(const std::wstring& p) {
    return p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\';
}

// True for UNC paths and mapped network drives. We never stat those: a dead
// share can block for seconds and this runs on a worker that must stay
// responsive.
bool IsNetworkPath(const std::wstring& p) {
    if (IsUnc(p)) return true;
    if (p.size() >= 3 && p[1] == L':' && p[2] == L'\\') {
        const std::wstring root = p.substr(0, 3);
        return ::GetDriveTypeW(root.c_str()) == DRIVE_REMOTE;
    }
    return false;
}

bool DirectoryExists(const std::wstring& p) {
    if (IsNetworkPath(p)) return true;  // assume alive, see above
    const DWORD attrs = ::GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// System noise the user never wants in the list, regardless of settings.
const std::vector<std::wstring>& BuiltInExcludes() {
    static const std::vector<std::wstring> list = [] {
        std::vector<std::wstring> v;
        auto add = [&v](const std::wstring& p) {
            if (!p.empty()) v.push_back(p);
        };
        add(TempDir());
        add(ExpandEnv(L"%LOCALAPPDATA%\\Temp"));
        add(ExpandEnv(L"%SystemRoot%\\Temp"));
        add(RecentDir());
        add(DataDir());
        return v;
    }();
    return list;
}

}  // namespace

std::wstring NormalizePath(const std::wstring& raw) {
    std::wstring p(TrimWs(raw));
    if (p.empty()) return {};

    // "C:\" keeps its slash, "C:\foo\" loses it.
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    if (p.size() == 2 && p[1] == L':') p.push_back(L'\\');

    // Canonical casing/short-name expansion, but only where it costs nothing.
    if (!IsNetworkPath(p)) {
        std::vector<wchar_t> buf(32768);
        const DWORD n = ::GetLongPathNameW(p.c_str(), buf.data(), static_cast<DWORD>(buf.size()));
        if (n > 0 && n < buf.size()) p.assign(buf.data(), n);
    }
    return p;
}

std::vector<FolderEntry> BuildFolderList(const Settings& settings, Snapshot& snap,
                                         PipelineStats* stats) {
    PipelineStats local;

    // 1. Collect.
    std::vector<RawEntry> raw = ReadOpenSaveMru();
    for (auto& e : ReadLastVisitedMru()) raw.push_back(std::move(e));
    for (auto& e : ReadRecentFolder()) raw.push_back(std::move(e));
    local.raw = raw.size();

    const std::vector<std::wstring> userExcludes = ExpandPatterns(settings.excludePaths);

    // 2..4. Normalize, filter, dedupe (newest timestamp wins).
    struct Pick {
        uint64_t ts = 0;
        Source source = Source::OpenSave;
    };
    std::map<std::wstring, Pick, PathLess> best;
    std::vector<std::wstring> seenPaths;

    for (const RawEntry& e : raw) {
        const std::wstring path = NormalizePath(e.path);
        if (path.empty()) continue;

        if (IsExcluded(path, BuiltInExcludes()) || IsExcluded(path, userExcludes)) {
            ++local.droppedExcluded;
            continue;
        }
        if (!DirectoryExists(path)) {
            ++local.droppedMissing;
            continue;
        }

        // Timestamp resolution. .lnk write times are real; registry entries only
        // get a fresh stamp when they sit at the head of their MRU list.
        const uint64_t known = snap.Get(path);
        uint64_t ts = known;
        if (e.source == Source::Recent || e.isHead) {
            ts = (std::max)(known, e.observedUtc);
        } else if (known == 0) {
            ts = e.observedUtc;
        }
        if (ts == 0) ts = e.observedUtc;

        if (const auto it = best.find(path); it != best.end()) {
            if (ts > it->second.ts) {
                it->second.ts = ts;
                it->second.source = e.source;
            }
        } else {
            best.emplace(path, Pick{ts, e.source});
            seenPaths.push_back(path);
        }
    }

    for (const auto& [path, pick] : best) snap.Set(path, pick.ts);
    snap.Prune(seenPaths);
    local.unique = best.size();

    // 5. Sort newest first, then truncate.
    std::vector<FolderEntry> result;
    result.reserve(best.size());
    for (const auto& [path, pick] : best) {
        result.push_back(FolderEntry{path, pick.ts, pick.source});
    }
    std::sort(result.begin(), result.end(), [](const FolderEntry& a, const FolderEntry& b) {
        if (a.lastUsedUtc != b.lastUsedUtc) return a.lastUsedUtc > b.lastUsedUtc;
        return PathLess{}(a.path, b.path);  // stable, deterministic output
    });
    if (result.size() > static_cast<size_t>(settings.maxFolders)) {
        result.resize(static_cast<size_t>(settings.maxFolders));
    }

    if (stats) *stats = local;
    return result;
}

}  // namespace lfs
