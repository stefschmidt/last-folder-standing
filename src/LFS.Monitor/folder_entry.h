// Shared vocabulary types for the collect -> filter -> emit pipeline.
#pragma once

#include <cstdint>
#include <string>

namespace lfs {

enum class Source { OpenSave, LastVisited, Recent, Explorer };

inline const wchar_t* SourceName(Source s) {
    switch (s) {
        case Source::OpenSave: return L"opensave";
        case Source::LastVisited: return L"lastvisited";
        case Source::Recent: return L"recent";
        case Source::Explorer: return L"explorer";
    }
    return L"unknown";
}

// Recent and Explorer entries carry a timestamp of the moment we actually
// observed the use. Registry entries only do so at the head of their MRU list.
inline bool HasRealTimestamp(Source s) {
    return s == Source::Recent || s == Source::Explorer;
}

// One raw observation from a source, before normalization and filtering.
struct RawEntry {
    std::wstring path;        // folder path as decoded, not yet normalized
    Source source = Source::OpenSave;
    int rank = 0;             // 0 = newest inside its group
    bool isHead = false;      // rank == 0, i.e. what caused the last write
    uint64_t observedUtc = 0; // registry key / .lnk write time (FILETIME ticks)
    std::wstring group;       // subkey or file name, diagnostics only
};

// A finished list item, ready to be written to state.json.
struct FolderEntry {
    std::wstring path;
    uint64_t lastUsedUtc = 0;
    Source source = Source::OpenSave;
};

}  // namespace lfs
