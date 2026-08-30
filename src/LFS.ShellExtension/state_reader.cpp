#include "state_reader.h"

#include "common/file_io.h"
#include "common/json.h"
#include "common/strings.h"

namespace lfs {

std::wstring LeafName(const std::wstring& path) {
    if (path.empty()) return {};

    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    if (end == 0) return path;

    const size_t slash = path.find_last_of(L"\\/", end - 1);
    if (slash == std::wstring::npos) return path.substr(0, end);

    // "C:\" and "\\server\share" have no meaningful leaf; show them whole.
    if (slash < 2) return path.substr(0, end);
    const std::wstring leaf = path.substr(slash + 1, end - slash - 1);
    return leaf.empty() ? path.substr(0, end) : leaf;
}

std::vector<StateFolder> ReadState() noexcept {
    std::vector<StateFolder> result;
    try {
        std::string bytes;
        if (!ReadFileBytes(StateFilePath(), bytes)) return result;

        const auto doc = json::Parse(Utf8ToWide(bytes));
        if (!doc || !doc->IsObject()) return result;

        // Unknown future versions are ignored rather than guessed at.
        const json::Value* version = doc->Find(L"version");
        if (!version || version->AsInt(0) != 1) return result;

        const json::Value* folders = doc->Find(L"folders");
        if (!folders || !folders->IsArray()) return result;

        for (size_t i = 0; i < folders->size(); ++i) {
            const json::Value* item = folders->At(i);
            if (!item || !item->IsObject()) continue;
            const json::Value* path = item->Find(L"path");
            if (!path || !path->IsString()) continue;

            StateFolder f;
            f.path = path->AsString();
            if (f.path.empty()) continue;
            f.displayName = LeafName(f.path);
            result.push_back(std::move(f));
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

}  // namespace lfs
