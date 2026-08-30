// settings.json (schema v1, see docs/DEVELOPMENT.md).
//
// Shared by the monitor (reads it, watches it) and the settings window (writes
// it). Reading never fails: anything unreadable or malformed yields defaults,
// because a broken settings file must not stop the monitor from working.
#pragma once

#include <string>
#include <vector>

#include "common/file_io.h"
#include "common/json.h"
#include "common/strings.h"

namespace lfs {

inline constexpr int kSettingsVersion = 1;
inline constexpr int kMinFolders = 1;
inline constexpr int kMaxFolders = 15;
inline constexpr int kDefaultFolders = 5;

struct Settings {
    int maxFolders = kDefaultFolders;
    std::vector<std::wstring> excludePaths;  // raw patterns, environment vars unexpanded

    bool operator==(const Settings& other) const {
        return maxFolders == other.maxFolders && excludePaths == other.excludePaths;
    }
    bool operator!=(const Settings& other) const { return !(*this == other); }
};

inline int ClampFolders(int n) {
    if (n < kMinFolders) return kMinFolders;
    if (n > kMaxFolders) return kMaxFolders;
    return n;
}

inline Settings LoadSettingsFrom(const std::wstring& path) {
    Settings s;

    std::string bytes;
    if (!ReadFileBytes(path, bytes)) return s;

    const auto doc = json::Parse(Utf8ToWide(bytes));
    if (!doc || !doc->IsObject()) return s;

    if (const json::Value* v = doc->Find(L"maxFolders"); v && v->IsNumber()) {
        s.maxFolders = ClampFolders(v->AsInt(kDefaultFolders));
    }

    if (const json::Value* v = doc->Find(L"excludePaths"); v && v->IsArray()) {
        for (size_t i = 0; i < v->size(); ++i) {
            const json::Value* item = v->At(i);
            if (!item || !item->IsString()) continue;
            std::wstring pattern(TrimWs(item->AsString()));
            if (!pattern.empty()) s.excludePaths.push_back(std::move(pattern));
        }
    }

    return s;
}

inline Settings LoadSettings() { return LoadSettingsFrom(SettingsFilePath()); }

inline bool SaveSettingsTo(const std::wstring& path, const Settings& s) {
    json::Value excludes = json::Value::MakeArray();
    for (const auto& p : s.excludePaths) excludes.Push(json::Value(p));

    json::Value root = json::Value::MakeObject();
    root.Set(L"version", json::Value(kSettingsVersion));
    root.Set(L"maxFolders", json::Value(ClampFolders(s.maxFolders)));
    root.Set(L"excludePaths", std::move(excludes));

    return WriteFileAtomic(path, WideToUtf8(json::Serialize(root)));
}

inline bool SaveSettings(const Settings& s) { return SaveSettingsTo(SettingsFilePath(), s); }

}  // namespace lfs
