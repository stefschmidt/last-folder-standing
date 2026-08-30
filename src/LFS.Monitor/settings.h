// Exclude matching. The settings file itself is handled by common/settings_io.h,
// which the settings window shares.
#pragma once

#include <string>
#include <vector>

#include "common/settings_io.h"

namespace lfs {

// True if `path` is covered by one of the patterns. A pattern containing no
// wildcard matches the folder itself and everything below it; a pattern with
// * or ? is matched against the whole path (case-insensitive).
bool IsExcluded(const std::wstring& path, const std::vector<std::wstring>& expandedPatterns);

// Expands environment variables and normalizes trailing slashes once, so the
// per-entry matching stays cheap.
std::vector<std::wstring> ExpandPatterns(const std::vector<std::wstring>& patterns);

}  // namespace lfs
