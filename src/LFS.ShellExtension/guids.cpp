// The one translation unit that actually defines our GUIDs.
//
// <initguid.h> turns the DEFINE_GUID in lfs_guid.h from a declaration into a
// definition. It must be included exactly once per binary, which is why this
// file contains nothing else.
#include <windows.h>

#include <initguid.h>

#include "common/lfs_guid.h"
