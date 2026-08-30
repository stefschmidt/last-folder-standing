#include "pidl.h"

#include <objbase.h>

#include <cstring>

namespace lfs {

PITEMID_CHILD CreateChildPidl(USHORT index, const std::wstring& path) {
    if (path.empty() || path.size() > kMaxChildPathChars) return nullptr;

    // header + path incl. NUL + the 2-byte list terminator
    const size_t pathBytes = (path.size() + 1) * sizeof(WCHAR);
    const size_t itemBytes = offsetof(ChildItem, path) + pathBytes;
    const size_t totalBytes = itemBytes + sizeof(USHORT);
    if (itemBytes > 0xFFFF) return nullptr;

    auto* raw = static_cast<BYTE*>(::CoTaskMemAlloc(totalBytes));
    if (!raw) return nullptr;
    ::ZeroMemory(raw, totalBytes);

    auto* item = reinterpret_cast<ChildItem*>(raw);
    item->cb = static_cast<USHORT>(itemBytes);
    item->signature = kChildSignature;
    item->version = kChildVersion;
    item->index = index;
    std::memcpy(item->path, path.c_str(), pathBytes);

    return reinterpret_cast<PITEMID_CHILD>(raw);
}

const ChildItem* AsChildItem(PCUITEMID_CHILD pidl) {
    if (!pidl) return nullptr;

    const USHORT cb = pidl->mkid.cb;
    if (cb < offsetof(ChildItem, path) + sizeof(WCHAR)) return nullptr;

    const auto* item = reinterpret_cast<const ChildItem*>(pidl);
    if (item->signature != kChildSignature) return nullptr;
    if (item->version != kChildVersion) return nullptr;

    // The path must be NUL-terminated inside the item, or reading it would run
    // off the end of the allocation.
    const size_t maxChars = (cb - offsetof(ChildItem, path)) / sizeof(WCHAR);
    if (maxChars == 0 || maxChars > kMaxChildPathChars + 1) return nullptr;
    bool terminated = false;
    for (size_t i = 0; i < maxChars; ++i) {
        if (item->path[i] == L'\0') {
            terminated = true;
            break;
        }
    }
    if (!terminated) return nullptr;

    return item;
}

std::wstring ChildPath(PCUITEMID_CHILD pidl) {
    const ChildItem* item = AsChildItem(pidl);
    if (!item) return {};
    try {
        return std::wstring(item->path);
    } catch (...) {
        return {};
    }
}

USHORT ChildIndex(PCUITEMID_CHILD pidl) {
    const ChildItem* item = AsChildItem(pidl);
    return item ? item->index : 0xFFFF;
}

}  // namespace lfs
