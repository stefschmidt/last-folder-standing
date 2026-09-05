// Reads state.json. This is the extension's entire view of the world
// (docs/DEVELOPMENT.md rule 1): no registry, no MRU parsing, no other I/O.
//
// Any problem -- missing file, bad JSON, wrong version, unreadable disk --
// yields an empty list. An empty node is a fine failure mode; an exception
// escaping into explorer.exe is not.
#pragma once

#include <string>
#include <vector>

#include "pidl.h"

namespace lfs {

struct StateFolder {
    std::wstring path;                     // full target path
    std::wstring displayName;              // what the tree shows, unique in the list
    NameStyle nameStyle = NameStyle::Leaf;  // how displayName was derived
};

std::vector<StateFolder> ReadState() noexcept;

// Leaf name of a path, used for the display name. "C:\" style roots keep the
// whole string because "C:" alone reads as a drive-relative path.
std::wstring LeafName(const std::wstring& path);

// Everything above the leaf, empty when there is nothing meaningful above it
// ("C:\", "\\server\share").
std::wstring ParentPath(const std::wstring& path);

// The display name of a path in the given style. Falls back to the leaf name
// when the style cannot be applied.
std::wstring FormatDisplayName(const std::wstring& path, NameStyle style);

}  // namespace lfs
