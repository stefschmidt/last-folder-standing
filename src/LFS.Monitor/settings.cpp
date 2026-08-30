#include "settings.h"

#include <windows.h>
#include <shlwapi.h>

#include "app_paths.h"
#include "common/strings.h"

namespace lfs {
namespace {

bool HasWildcard(const std::wstring& s) {
    return s.find(L'*') != std::wstring::npos || s.find(L'?') != std::wstring::npos;
}

std::wstring StripTrailingSlashes(std::wstring s) {
    while (s.size() > 1 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

}  // namespace

std::vector<std::wstring> ExpandPatterns(const std::vector<std::wstring>& patterns) {
    std::vector<std::wstring> out;
    out.reserve(patterns.size());
    for (const auto& p : patterns) {
        std::wstring expanded = StripTrailingSlashes(ExpandEnv(p));
        if (!expanded.empty()) out.push_back(std::move(expanded));
    }
    return out;
}

bool IsExcluded(const std::wstring& path, const std::vector<std::wstring>& expandedPatterns) {
    for (const auto& pattern : expandedPatterns) {
        if (HasWildcard(pattern)) {
            if (::PathMatchSpecExW(path.c_str(), pattern.c_str(), PMSF_NORMAL) == S_OK) return true;
        } else {
            // Plain path: the folder itself or anything under it.
            if (IEquals(path, pattern)) return true;
            if (path.size() > pattern.size() && path[pattern.size()] == L'\\' &&
                IStartsWith(path, pattern)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace lfs
