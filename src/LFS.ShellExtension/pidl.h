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

// How a child's display name is built. It travels in the item because the shell
// asks for the name long after enumeration, when the rest of the list -- and
// with it the knowledge that two folders share a leaf name -- is gone.
enum class NameStyle : USHORT {
    Leaf = 0,            // "WindowsInstaller"
    LeafWithParent = 1,  // "WindowsInstaller (Projekt A)"
    FullPath = 2,        // "D:\Projekt A\WindowsInstaller"
};

#include <pshpack1.h>
struct ChildItem {
    USHORT cb;         // total size of this SHITEMID, including cb itself
    USHORT signature;  // kChildSignature
    USHORT version;    // kChildVersion
    USHORT index;      // position in state.json, defines sort order
    USHORT nameStyle;  // NameStyle; an unknown value falls back to Leaf
    WCHAR path[1];     // NUL-terminated target path, variable length
};
#include <poppack.h>

inline constexpr USHORT kChildSignature = 0x534C;  // 'LS'
// 2 added nameStyle, which moved the path. Items from version 1 have a different
// layout and are rejected like any foreign PIDL -- the shell then re-enumerates.
inline constexpr USHORT kChildVersion = 2;
// A path longer than this is not something we wrote.
inline constexpr size_t kMaxChildPathChars = 4096;

// CoTaskMemAlloc'd single-item ITEMIDLIST. Returns nullptr on failure.
PITEMID_CHILD CreateChildPidl(USHORT index, const std::wstring& path, NameStyle style);

// nullptr unless the PIDL is a well-formed item of ours.
const ChildItem* AsChildItem(PCUITEMID_CHILD pidl);

// Empty if the PIDL is not ours.
std::wstring ChildPath(PCUITEMID_CHILD pidl);

// 0xFFFF if the PIDL is not ours, which sorts such items last.
USHORT ChildIndex(PCUITEMID_CHILD pidl);

// The style stored in an already validated item. An unknown value, e.g. from a
// future build, falls back to NameStyle::Leaf.
NameStyle ItemNameStyle(const ChildItem* item);

// NameStyle::Leaf if the PIDL is not ours or carries an unknown style.
NameStyle ChildNameStyle(PCUITEMID_CHILD pidl);

}  // namespace lfs
