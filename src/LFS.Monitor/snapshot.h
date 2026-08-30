// Remembers when we first saw / last promoted each folder.
//
// The registry MRU carries no timestamps. A key's last-write time only proves
// something about its *newest* entry, so for everything else we keep our own
// record - otherwise every unrelated dialog write would shuffle the whole list.
// Persisted so a restart does not re-stamp all folders with "now".
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

namespace lfs {

struct PathLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                      static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    }
};

class Snapshot {
public:
    void Load();                 // missing/corrupt file = empty snapshot
    bool Save();                 // no-op if nothing changed since the last save

    uint64_t Get(const std::wstring& path) const;  // 0 = unknown
    void Set(const std::wstring& path, uint64_t timestampUtc);

    // Drops everything not in `keep` so the file cannot grow without bound.
    void Prune(const std::vector<std::wstring>& keep);

    size_t size() const { return entries_.size(); }

private:
    std::map<std::wstring, uint64_t, PathLess> entries_;
    bool dirty_ = false;
};

}  // namespace lfs
