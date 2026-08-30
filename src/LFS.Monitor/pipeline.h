// Collect -> normalize -> filter -> dedupe -> sort -> truncate.
// The one place that decides what the folder list actually is.
#pragma once

#include <string>
#include <vector>

#include "folder_entry.h"
#include "settings.h"
#include "snapshot.h"

namespace lfs {

struct PipelineStats {
    size_t raw = 0;
    size_t droppedMissing = 0;
    size_t droppedExcluded = 0;
    size_t unique = 0;
};

// Updates `snap` with the resolved timestamps (caller decides when to Save()).
std::vector<FolderEntry> BuildFolderList(const Settings& settings, Snapshot& snap,
                                         PipelineStats* stats = nullptr);

// Exposed for the console mode's diagnostics.
std::wstring NormalizePath(const std::wstring& raw);

}  // namespace lfs
