#include "state_reader.h"

#include "common/file_io.h"
#include "common/json.h"
#include "common/strings.h"

namespace lfs {
namespace {

size_t CountName(const std::vector<StateFolder>& folders, const std::wstring& name) {
    size_t count = 0;
    for (const auto& folder : folders) {
        if (IEquals(folder.displayName, name)) ++count;
    }
    return count;
}

// Two entries called "WindowsInstaller" are useless in a list you pick a copy
// target from. Widen the names that collide until every entry is unique: first
// by the parent folder, and if that still collides, by the full path.
//
// Which entries collide is decided up front, on the original names. Deciding it
// while rewriting would leave the second of two twins with the short name,
// because by then the first one no longer collides with anything.
void MakeNamesUnique(std::vector<StateFolder>& folders) {
    std::vector<bool> collides(folders.size(), false);
    for (size_t i = 0; i < folders.size(); ++i) {
        collides[i] = CountName(folders, folders[i].displayName) > 1;
    }
    for (size_t i = 0; i < folders.size(); ++i) {
        if (!collides[i]) continue;
        std::wstring widened = FormatDisplayName(folders[i].path, NameStyle::LeafWithParent);
        if (widened == folders[i].displayName) continue;  // nothing above the leaf
        folders[i].nameStyle = NameStyle::LeafWithParent;
        folders[i].displayName = std::move(widened);
    }

    // Same leaf under a same-named parent in different branches, e.g.
    // C:\A\build\out and D:\B\build\out. Only the full path separates those.
    for (size_t i = 0; i < folders.size(); ++i) {
        collides[i] = CountName(folders, folders[i].displayName) > 1;
    }
    for (size_t i = 0; i < folders.size(); ++i) {
        if (!collides[i]) continue;
        folders[i].nameStyle = NameStyle::FullPath;
        folders[i].displayName = folders[i].path;
    }
}

}  // namespace

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

std::wstring ParentPath(const std::wstring& path) {
    if (path.empty()) return {};

    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    if (end == 0) return {};

    const size_t slash = path.find_last_of(L"\\/", end - 1);
    // npos: a bare name. Below 2: the separator belongs to "C:\" or to the "\\"
    // of a UNC path, so what is left is not a parent folder.
    if (slash == std::wstring::npos || slash < 2) return {};
    return path.substr(0, slash);
}

std::wstring FormatDisplayName(const std::wstring& path, NameStyle style) {
    if (style == NameStyle::FullPath) return path;
    if (style == NameStyle::LeafWithParent) {
        const std::wstring parent = LeafName(ParentPath(path));
        if (!parent.empty()) return LeafName(path) + L" (" + parent + L")";
    }
    return LeafName(path);
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

        MakeNamesUnique(result);
    } catch (...) {
        result.clear();
    }
    return result;
}

}  // namespace lfs
