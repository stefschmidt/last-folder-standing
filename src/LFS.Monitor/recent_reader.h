// Reads %APPDATA%\Microsoft\Windows\Recent\*.lnk (SHAddToRecentDocs clients,
// e.g. Office). Absent or empty folder is a normal state, not an error.
#pragma once

#include <vector>

#include "folder_entry.h"

namespace lfs {

// Only the newest `maxFiles` .lnk files are opened; the folder can hold
// thousands and we never need more than a handful of results.
std::vector<RawEntry> ReadRecentFolder(size_t maxFiles = 200);

}  // namespace lfs
