// Reads the common-dialog MRU keys under
// HKCU\...\Explorer\ComDlg32 and turns them into folder paths.
//
// Everything here must survive garbage: the values are undocumented binary
// PIDLs written by every application on the system.
#pragma once

#include <vector>

#include "folder_entry.h"

namespace lfs {

inline constexpr wchar_t kComDlg32Key[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32";

// OpenSavePidlMRU: one subkey per file extension, PIDL points at the *file*,
// so the parent folder is what we want.
std::vector<RawEntry> ReadOpenSaveMru();

// LastVisitedPidlMRU (+ the "Legacy" variant on newer builds): executable name
// followed by the PIDL of the folder that app last used.
std::vector<RawEntry> ReadLastVisitedMru();

}  // namespace lfs
