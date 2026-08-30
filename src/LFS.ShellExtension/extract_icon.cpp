#include "extract_icon.h"

#include <shellapi.h>

#include "dll.h"

namespace lfs {

FolderIcon::FolderIcon(std::wstring targetPath) : path_(std::move(targetPath)) { DllAddRef(); }

FolderIcon::~FolderIcon() { DllRelease(); }

IFACEMETHODIMP FolderIcon::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IExtractIconW) {
        *ppv = static_cast<IExtractIconW*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) FolderIcon::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&refCount_));
}

IFACEMETHODIMP_(ULONG) FolderIcon::Release() {
    const long count = ::InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
}

IFACEMETHODIMP FolderIcon::GetIconLocation(UINT uFlags, PWSTR pszIconFile, UINT cchMax,
                                           int* piIndex, UINT* pwFlags) {
    (void)uFlags;
    if (!pszIconFile || !piIndex || !pwFlags || cchMax == 0) return E_INVALIDARG;

    SHFILEINFOW info{};
    // USEFILEATTRIBUTES: derive the icon from "this is a directory" instead of
    // opening the path. A dead network share must not stall the shell here.
    const DWORD_PTR ok = ::SHGetFileInfoW(path_.c_str(), FILE_ATTRIBUTE_DIRECTORY, &info,
                                          sizeof(info),
                                          SHGFI_ICONLOCATION | SHGFI_USEFILEATTRIBUTES);
    if (!ok || info.szDisplayName[0] == L'\0') {
        // Generic folder icon from the shell's own resources.
        ::wcscpy_s(pszIconFile, cchMax, L"%SystemRoot%\\System32\\shell32.dll");
        *piIndex = 3;
        *pwFlags = GIL_PERCLASS | GIL_NOTFILENAME;
        return S_OK;
    }

    ::wcscpy_s(pszIconFile, cchMax, info.szDisplayName);
    *piIndex = info.iIcon;
    *pwFlags = GIL_PERCLASS;
    return S_OK;
}

IFACEMETHODIMP FolderIcon::Extract(PCWSTR pszFile, UINT nIconIndex, HICON* phiconLarge,
                                   HICON* phiconSmall, UINT nIconSize) {
    (void)pszFile;
    (void)nIconIndex;
    (void)phiconLarge;
    (void)phiconSmall;
    (void)nIconSize;
    // S_FALSE: let the shell load the icon from the location we reported.
    return S_FALSE;
}

}  // namespace lfs
