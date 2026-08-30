// Reads state.json. This is the extension's entire view of the world
// (docs/DEVELOPMENT.md rule 1): no registry, no MRU parsing, no other I/O.
//
// Any problem -- missing file, bad JSON, wrong version, unreadable disk --
// yields an empty list. An empty node is a fine failure mode; an exception
// escaping into explorer.exe is not.
#pragma once

#include <string>
#include <vector>

namespace lfs {

struct StateFolder {
    std::wstring path;         // full target path
    std::wstring displayName;  // leaf name, what the tree shows
};

std::vector<StateFolder> ReadState() noexcept;

// Leaf name of a path, used for the display name. "C:\" style roots keep the
// whole string because "C:" alone reads as a drive-relative path.
std::wstring LeafName(const std::wstring& path);

}  // namespace lfs
