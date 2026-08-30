#include "snapshot.h"

#include "app_paths.h"
#include "common/json.h"
#include "common/strings.h"
#include "common/timeutil.h"

namespace lfs {

void Snapshot::Load() {
    entries_.clear();
    dirty_ = false;

    std::string bytes;
    if (!ReadFileBytes(SnapshotPath(), bytes)) return;

    const auto doc = json::Parse(Utf8ToWide(bytes));
    if (!doc || !doc->IsObject()) return;

    const json::Value* list = doc->Find(L"entries");
    if (!list || !list->IsArray()) return;

    for (size_t i = 0; i < list->size(); ++i) {
        const json::Value* item = list->At(i);
        if (!item || !item->IsObject()) continue;
        const json::Value* path = item->Find(L"path");
        const json::Value* used = item->Find(L"lastUsedUtc");
        if (!path || !path->IsString() || !used || !used->IsString()) continue;
        const uint64_t ts = FromIso8601(used->AsString());
        if (ts == 0) continue;
        entries_[path->AsString()] = ts;
    }
}

bool Snapshot::Save() {
    if (!dirty_) return true;

    json::Value list = json::Value::MakeArray();
    for (const auto& [path, ts] : entries_) {
        json::Value item = json::Value::MakeObject();
        item.Set(L"path", json::Value(path));
        item.Set(L"lastUsedUtc", json::Value(ToIso8601(ts)));
        list.Push(std::move(item));
    }

    json::Value root = json::Value::MakeObject();
    root.Set(L"version", json::Value(1));
    root.Set(L"entries", std::move(list));

    if (!WriteFileAtomic(SnapshotPath(), WideToUtf8(json::Serialize(root)))) return false;
    dirty_ = false;
    return true;
}

uint64_t Snapshot::Get(const std::wstring& path) const {
    const auto it = entries_.find(path);
    return it == entries_.end() ? 0 : it->second;
}

void Snapshot::Set(const std::wstring& path, uint64_t timestampUtc) {
    const auto it = entries_.find(path);
    if (it != entries_.end() && it->second == timestampUtc) return;
    entries_[path] = timestampUtc;
    dirty_ = true;
}

void Snapshot::Prune(const std::vector<std::wstring>& keep) {
    std::map<std::wstring, uint64_t, PathLess> kept;
    for (const auto& path : keep) {
        const auto it = entries_.find(path);
        if (it != entries_.end()) kept.emplace(it->first, it->second);
    }
    if (kept.size() != entries_.size()) {
        entries_.swap(kept);
        dirty_ = true;
    }
}

}  // namespace lfs
