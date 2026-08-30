// Module-wide state. DllMain does nothing but record the instance handle
// (docs/DEVELOPMENT.md rule 1: this DLL lives inside explorer.exe).
#pragma once

#include <windows.h>

namespace lfs {

void DllAddRef();
void DllRelease();
long DllObjectCount();

HINSTANCE DllInstance();
void SetDllInstance(HINSTANCE instance);

}  // namespace lfs
