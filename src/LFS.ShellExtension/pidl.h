// Our child item ID format.
//
// A child of the root node is one folder from state.json. The shell hands these
// blobs back to us later, and they can also come from a *different* build or from
// a corrupted view state -- so every read validates before trusting the content.
#pragma once

#include <windows.h>
#include <shtypes.h>

#include <string>

namespace lfs {

#include <pshpack1.h>
struct ChildItem {
    USHORT cb;         // total size of this SHITEMID, including cb itself
    USHORT signature;  // kChildSignature
    USHORT version;    // kChildVersion
    USHORT index;      // position in state.json, defines sort order
    WCHAR path[1];     // NUL-terminated target path, variable length
};
#include <poppack.h>

inline constexpr USHORT kChildSignature = 0x534C;  // 'LS'
inline constexpr USHORT kChildVersion = 1;
// A path longer than this is not something we wrote.
inline constexpr size_t kMaxChildPathChars = 4096;

// CoTaskMemAlloc'd single-item ITEMIDLIST. Returns nullptr on failure.
PITEMID_CHILD CreateChildPidl(USHORT index, const std::wstring& path);

// nullptr unless the PIDL is a well-formed item of ours.
const ChildItem* AsChildItem(PCUITEMID_CHILD pidl);

// Empty if the PIDL is not ours.
std::wstring ChildPath(PCUITEMID_CHILD pidl);

// 0xFFFF if the PIDL is not ours, which sorts such items last.
USHORT ChildIndex(PCUITEMID_CHILD pidl);

}  // namespace lfs
