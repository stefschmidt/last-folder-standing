// DLL entry points and per-user registration.
//
// Registration is HKCU only, so regsvr32 works without elevation and an
// uninstall never leaves machine-wide leftovers.
#include "dll.h"

#include <windows.h>
#include <new>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>

#include "class_factory.h"
#include "common/lfs_guid.h"
#include "root_folder.h"

namespace lfs {
namespace {

long g_objectCount = 0;
HINSTANCE g_instance = nullptr;

constexpr wchar_t kClsidKey[] = L"Software\\Classes\\CLSID\\" LFS_CLSID_STRING;
constexpr wchar_t kNamespaceKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\" LFS_CLSID_STRING;
constexpr wchar_t kHideDesktopKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel";

// Folder attributes reported before the object is even created; the shell reads
// these from the registry to decide how to show the node. Same value the folder
// reports at runtime -- see kRootAttributes for why FILESYSANCESTOR is in there.
constexpr DWORD kFolderAttributes = static_cast<DWORD>(kRootAttributes);
// Position in the navigation pane. Below the cloud providers, above This PC.
constexpr DWORD kSortOrderIndex = 0x42;

std::wstring ModulePath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(g_instance, buffer, ARRAYSIZE(buffer));
    if (n == 0 || n >= ARRAYSIZE(buffer)) return {};
    return buffer;
}

LSTATUS SetString(HKEY parent, const wchar_t* subKey, const wchar_t* valueName,
                  const std::wstring& data, DWORD type = REG_SZ) {
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(parent, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                   KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = ::RegSetValueExW(key, valueName, 0, type,
                          reinterpret_cast<const BYTE*>(data.c_str()),
                          static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return rc;
}

LSTATUS SetDword(HKEY parent, const wchar_t* subKey, const wchar_t* valueName, DWORD data) {
    HKEY key = nullptr;
    LSTATUS rc = ::RegCreateKeyExW(parent, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                   KEY_WRITE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = ::RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data),
                          sizeof(data));
    ::RegCloseKey(key);
    return rc;
}

}  // namespace

void DllAddRef() { ::InterlockedIncrement(&g_objectCount); }
void DllRelease() { ::InterlockedDecrement(&g_objectCount); }
long DllObjectCount() { return g_objectCount; }
HINSTANCE DllInstance() { return g_instance; }
void SetDllInstance(HINSTANCE instance) { g_instance = instance; }

}  // namespace lfs

// --- exports ---------------------------------------------------------------

STDAPI_(BOOL) DllMain(HINSTANCE instance, DWORD reason, void* reserved) {
    (void)reserved;
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            lfs::SetDllInstance(instance);
            // Nothing else. No CRT-heavy work, no thread notifications.
            ::DisableThreadLibraryCalls(instance);
            break;
        default: break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_LastFolderStanding) return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new (std::nothrow) lfs::ClassFactory();
    if (!factory) return E_OUTOFMEMORY;

    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return lfs::DllObjectCount() == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    const std::wstring dllPath = lfs::ModulePath();
    if (dllPath.empty()) return HRESULT_FROM_WIN32(::GetLastError());

    const std::wstring clsidKey = lfs::kClsidKey;
    const std::wstring inproc = clsidKey + L"\\InprocServer32";
    const std::wstring shellFolder = clsidKey + L"\\ShellFolder";
    const std::wstring defaultIcon = clsidKey + L"\\DefaultIcon";

    LSTATUS rc = lfs::SetString(HKEY_CURRENT_USER, clsidKey.c_str(), nullptr, LFS_DISPLAY_NAME);
    if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);

    // Pins the node into the navigation pane instead of only the desktop.
    lfs::SetDword(HKEY_CURRENT_USER, clsidKey.c_str(), L"System.IsPinnedToNameSpaceTree", 1);
    lfs::SetDword(HKEY_CURRENT_USER, clsidKey.c_str(), L"SortOrderIndex", lfs::kSortOrderIndex);

    // Icon resource 1 of this DLL is app.ico.
    lfs::SetString(HKEY_CURRENT_USER, defaultIcon.c_str(), nullptr, dllPath + L",0");

    rc = lfs::SetString(HKEY_CURRENT_USER, inproc.c_str(), nullptr, dllPath);
    if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);
    lfs::SetString(HKEY_CURRENT_USER, inproc.c_str(), L"ThreadingModel", L"Apartment");

    lfs::SetDword(HKEY_CURRENT_USER, shellFolder.c_str(), L"Attributes", lfs::kFolderAttributes);

    rc = lfs::SetString(HKEY_CURRENT_USER, lfs::kNamespaceKey, nullptr, LFS_DISPLAY_NAME);
    if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);

    // Keep it out of the desktop icon list; the nav pane is the point.
    lfs::SetDword(HKEY_CURRENT_USER, lfs::kHideDesktopKey, LFS_CLSID_STRING, 1);

    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    ::SHDeleteKeyW(HKEY_CURRENT_USER, lfs::kNamespaceKey);
    ::SHDeleteKeyW(HKEY_CURRENT_USER, lfs::kClsidKey);

    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, lfs::kHideDesktopKey, 0, KEY_SET_VALUE, &key) ==
        ERROR_SUCCESS) {
        ::RegDeleteValueW(key, LFS_CLSID_STRING);
        ::RegCloseKey(key);
    }

    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
