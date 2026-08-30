// Well-known paths for the monitor process. File helpers live in
// common/file_io.h, which this header pulls in for its users.
#pragma once

#include <string>

#include "common/file_io.h"  // ReadFileBytes, WriteFileAtomic, ExpandEnv, DataDirPath

namespace lfs {

// %LOCALAPPDATA%\LastFolderStanding -- created on first call. Empty on failure.
const std::wstring& DataDir();

const std::wstring& StatePath();     // <DataDir>\state.json
const std::wstring& SettingsPath();  // <DataDir>\settings.json
const std::wstring& SnapshotPath();  // <DataDir>\mru_snapshot.json

// %APPDATA%\Microsoft\Windows\Recent -- may not exist (tracking disabled).
const std::wstring& RecentDir();

// %TEMP%, used by the built-in exclude list.
const std::wstring& TempDir();

}  // namespace lfs
