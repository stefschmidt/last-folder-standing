// The namespace extension's CLSID. Referenced by the DLL (registration and
// class factory) and by the monitor (to address SHChangeNotify at our root).
//
// This value is part of the installed footprint: changing it orphans the
// registry entries of every existing installation.
#pragma once

#include <guiddef.h>

// {30B40AF0-3F96-435B-9A3E-301454A92D98}
DEFINE_GUID(CLSID_LastFolderStanding, 0x30b40af0, 0x3f96, 0x435b, 0x9a, 0x3e, 0x30, 0x14, 0x54,
            0xa9, 0x2d, 0x98);

#define LFS_CLSID_STRING L"{30B40AF0-3F96-435B-9A3E-301454A92D98}"
#define LFS_DISPLAY_NAME L"Last Folder Standing"
// Parsing name of our root, e.g. for SHParseDisplayName.
#define LFS_SHELL_PATH L"shell:::" LFS_CLSID_STRING
