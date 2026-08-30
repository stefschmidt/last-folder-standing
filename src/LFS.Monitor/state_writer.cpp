#include "state_writer.h"

#include <windows.h>
#include <shlobj.h>

#include "app_paths.h"
#include "common/json.h"
#include "common/lfs_guid.h"
#include "common/strings.h"
#include "common/timeutil.h"

namespace lfs {
namespace {

bool SameList(const std::vector<FolderEntry>& a, const std::vector<FolderEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].lastUsedUtc != b[i].lastUsedUtc) return false;
        if (!IEquals(a[i].path, b[i].path)) return false;
        if (a[i].source != b[i].source) return false;
    }
    return true;
}

Source SourceFromName(const std::wstring& name) {
    if (name == L"recent") return Source::Recent;
    if (name == L"lastvisited") return Source::LastVisited;
    return Source::OpenSave;
}

}  // namespace

std::wstring StateWriter::Serialize(const std::vector<FolderEntry>& folders) {
    json::Value list = json::Value::MakeArray();
    for (const auto& f : folders) {
        json::Value item = json::Value::MakeObject();
        item.Set(L"path", json::Value(f.path));
        item.Set(L"lastUsedUtc", json::Value(ToIso8601(f.lastUsedUtc)));
        item.Set(L"source", json::Value(SourceName(f.source)));
        list.Push(std::move(item));
    }

    json::Value root = json::Value::MakeObject();
    root.Set(L"version", json::Value(kStateVersion));
    root.Set(L"updatedUtc", json::Value(ToIso8601(NowUtc())));
    root.Set(L"folders", std::move(list));
    return json::Serialize(root);
}

void StateWriter::LoadExisting() {
    last_.clear();
    haveLast_ = false;

    std::string bytes;
    if (!ReadFileBytes(StatePath(), bytes)) return;

    const auto doc = json::Parse(Utf8ToWide(bytes));
    if (!doc || !doc->IsObject()) return;
    const json::Value* version = doc->Find(L"version");
    if (!version || version->AsInt(0) != kStateVersion) return;
    const json::Value* list = doc->Find(L"folders");
    if (!list || !list->IsArray()) return;

    for (size_t i = 0; i < list->size(); ++i) {
        const json::Value* item = list->At(i);
        if (!item || !item->IsObject()) continue;
        const json::Value* path = item->Find(L"path");
        const json::Value* used = item->Find(L"lastUsedUtc");
        if (!path || !path->IsString()) continue;
        FolderEntry e;
        e.path = path->AsString();
        e.lastUsedUtc = used && used->IsString() ? FromIso8601(used->AsString()) : 0;
        if (const json::Value* src = item->Find(L"source"); src && src->IsString()) {
            e.source = SourceFromName(src->AsString());
        }
        last_.push_back(std::move(e));
    }
    haveLast_ = true;
}

bool StateWriter::Write(const std::vector<FolderEntry>& folders) {
    if (haveLast_ && SameList(folders, last_)) return false;

    if (!WriteFileAtomic(StatePath(), WideToUtf8(Serialize(folders)))) return false;

    last_ = folders;
    haveLast_ = true;

    // Tell the shell that our namespace node changed, so an open Explorer window
    // or file dialog re-enumerates. Fails harmlessly while the extension is not
    // registered yet.
    PIDLIST_ABSOLUTE root = nullptr;
    if (SUCCEEDED(::SHParseDisplayName(LFS_SHELL_PATH, nullptr, &root, 0, nullptr))) {
        ::SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, root, nullptr);
        ::CoTaskMemFree(root);
    }
    return true;
}

}  // namespace lfs
