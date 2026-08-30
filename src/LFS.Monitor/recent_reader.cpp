#include "recent_reader.h"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <string>

#include "app_paths.h"
#include "common/timeutil.h"

namespace lfs {
namespace {

struct LinkFile {
    std::wstring name;
    uint64_t writeTime = 0;
};

// Deliberately no IShellLink::Resolve. Resolve may hit the network, MSI or the
// link tracking service; a dead share would stall the monitor for seconds.
// The raw stored path is what we want anyway - if it is stale, the existence
// check in the pipeline drops it.
std::wstring RawTargetOf(IShellLinkW* link, IPersistFile* persist, const std::wstring& lnkPath) {
    if (FAILED(persist->Load(lnkPath.c_str(), STGM_READ))) return {};

    wchar_t target[MAX_PATH]{};
    if (FAILED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH))) return {};
    if (target[0] == L'\0') return {};
    return target;
}

bool LooksLikeFile(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    const size_t dot = path.find_last_of(L'.');
    return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash + 1);
}

std::wstring ParentOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash < 2) return {};
    if (slash == 2 && path[1] == L':') return path.substr(0, 3);  // "C:\"
    return path.substr(0, slash);
}

}  // namespace

std::vector<RawEntry> ReadRecentFolder(size_t maxFiles) {
    std::vector<RawEntry> out;

    const std::wstring& dir = RecentDir();
    if (dir.empty()) return out;

    // 1. List the .lnk files with their write times (cheap, no COM yet).
    std::vector<LinkFile> links;
    const std::wstring pattern = dir + L"\\*.lnk";
    WIN32_FIND_DATAW fd{};
    const HANDLE find = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                           FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        LinkFile lf;
        lf.name = fd.cFileName;
        lf.writeTime = FileTimeToU64(fd.ftLastWriteTime);
        links.push_back(std::move(lf));
    } while (::FindNextFileW(find, &fd));
    ::FindClose(find);

    if (links.empty()) return out;

    std::sort(links.begin(), links.end(),
              [](const LinkFile& a, const LinkFile& b) { return a.writeTime > b.writeTime; });
    if (links.size() > maxFiles) links.resize(maxFiles);

    // 2. Read the targets. One IShellLink instance is reused for all files.
    IShellLinkW* link = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link)))) {
        return out;
    }
    IPersistFile* persist = nullptr;
    if (FAILED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist)))) {
        link->Release();
        return out;
    }

    int rank = 0;
    for (const LinkFile& lf : links) {
        const std::wstring target = RawTargetOf(link, persist, dir + L"\\" + lf.name);
        if (target.empty()) continue;

        std::wstring folder;
        const DWORD attrs = ::GetFileAttributesW(target.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            folder = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? target : ParentOf(target);
        } else {
            // Unreachable or gone: guess from the name instead of touching it again.
            folder = LooksLikeFile(target) ? ParentOf(target) : target;
        }
        if (folder.empty()) continue;

        RawEntry e;
        e.path = std::move(folder);
        e.source = Source::Recent;
        e.rank = rank++;
        e.isHead = (e.rank == 0);
        e.observedUtc = lf.writeTime;  // real timestamp, unlike the registry sources
        e.group = lf.name;
        out.push_back(std::move(e));
    }

    persist->Release();
    link->Release();
    return out;
}

}  // namespace lfs
