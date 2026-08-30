#include "root_folder.h"

#include <shlwapi.h>

#include <new>
#include <string>
#include <vector>

#include "common/lfs_guid.h"
#include "dll.h"
#include "enum_children.h"
#include "extract_icon.h"
#include "pidl.h"
#include "state_reader.h"

namespace lfs {
namespace {

// What a child claims to be. SFGAO_FILESYSTEM plus a real parsing name is what
// lets the shell treat the item as the target folder, so a click navigates
// there instead of into a virtual view of ours.
constexpr SFGAOF kChildAttributes = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR |
                                    SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE;

HRESULT StringToStrRet(const std::wstring& text, STRRET* out) {
    if (!out) return E_POINTER;
    out->uType = STRRET_WSTR;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    out->pOleStr = static_cast<LPOLESTR>(::CoTaskMemAlloc(bytes));
    if (!out->pOleStr) return E_OUTOFMEMORY;
    ::memcpy(out->pOleStr, text.c_str(), bytes);
    return S_OK;
}

// The single item of a child PIDL, or nullptr if the PIDL is not one of ours.
PCUITEMID_CHILD AsChild(PCUIDLIST_RELATIVE pidl) {
    const auto child = reinterpret_cast<PCUITEMID_CHILD>(pidl);
    return AsChildItem(child) ? child : nullptr;
}

}  // namespace

RootFolder::RootFolder() { DllAddRef(); }

RootFolder::~RootFolder() {
    if (rootPidl_) ::CoTaskMemFree(rootPidl_);
    DllRelease();
}

IFACEMETHODIMP RootFolder::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IShellFolder || riid == IID_IShellFolder2) {
        *ppv = static_cast<IShellFolder2*>(this);
    } else if (riid == IID_IPersist || riid == IID_IPersistFolder ||
               riid == IID_IPersistFolder2) {
        *ppv = static_cast<IPersistFolder2*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) RootFolder::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&refCount_));
}

IFACEMETHODIMP_(ULONG) RootFolder::Release() {
    const long count = ::InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
}

// --- IPersistFolder2 -------------------------------------------------------

IFACEMETHODIMP RootFolder::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_LastFolderStanding;
    return S_OK;
}

IFACEMETHODIMP RootFolder::Initialize(PCIDLIST_ABSOLUTE pidl) {
    if (rootPidl_) {
        ::CoTaskMemFree(rootPidl_);
        rootPidl_ = nullptr;
    }
    if (!pidl) return S_OK;
    rootPidl_ = ::ILCloneFull(pidl);
    return rootPidl_ ? S_OK : E_OUTOFMEMORY;
}

IFACEMETHODIMP RootFolder::GetCurFolder(PIDLIST_ABSOLUTE* ppidl) {
    if (!ppidl) return E_POINTER;
    *ppidl = nullptr;
    if (!rootPidl_) return S_FALSE;
    *ppidl = ::ILCloneFull(rootPidl_);
    return *ppidl ? S_OK : E_OUTOFMEMORY;
}

// --- IShellFolder ----------------------------------------------------------

IFACEMETHODIMP RootFolder::ParseDisplayName(HWND hwnd, IBindCtx* pbc, PWSTR pszDisplayName,
                                            ULONG* pchEaten, PIDLIST_RELATIVE* ppidl,
                                            ULONG* pdwAttributes) {
    (void)hwnd;
    (void)pbc;
    if (!ppidl) return E_POINTER;
    *ppidl = nullptr;
    if (pchEaten) *pchEaten = 0;
    if (!pszDisplayName) return E_INVALIDARG;

    // Match by target path against the current list; anything else is unknown.
    try {
        const std::vector<StateFolder> folders = ReadState();
        for (size_t i = 0; i < folders.size(); ++i) {
            if (::StrCmpIW(folders[i].path.c_str(), pszDisplayName) != 0) continue;
            PITEMID_CHILD child = CreateChildPidl(static_cast<USHORT>(i), folders[i].path);
            if (!child) return E_OUTOFMEMORY;
            *ppidl = reinterpret_cast<PIDLIST_RELATIVE>(child);
            if (pdwAttributes) *pdwAttributes &= kChildAttributes;
            if (pchEaten) *pchEaten = static_cast<ULONG>(::wcslen(pszDisplayName));
            return S_OK;
        }
    } catch (...) {
        return E_FAIL;
    }
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

IFACEMETHODIMP RootFolder::EnumObjects(HWND hwnd, SHCONTF grfFlags, IEnumIDList** ppenumIDList) {
    (void)hwnd;
    if (!ppenumIDList) return E_POINTER;
    *ppenumIDList = nullptr;

    // Everything we offer is a folder; a caller asking only for files gets none.
    if (!(grfFlags & SHCONTF_FOLDERS)) {
        auto* empty = new (std::nothrow) ChildEnumerator(std::vector<StateFolder>{});
        if (!empty) return E_OUTOFMEMORY;
        *ppenumIDList = empty;
        return S_OK;
    }

    try {
        auto* enumerator = new (std::nothrow) ChildEnumerator(ReadState());
        if (!enumerator) return E_OUTOFMEMORY;
        *ppenumIDList = enumerator;
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

IFACEMETHODIMP RootFolder::BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid,
                                        void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    const std::wstring path = ChildPath(AsChild(pidl));
    if (path.empty()) return E_INVALIDARG;

    // Hand the caller the real folder. This is the only place that can block
    // (a dead share), and it only runs when the user actually opens that item.
    PIDLIST_ABSOLUTE target = nullptr;
    HRESULT hr = ::SHParseDisplayName(path.c_str(), pbc, &target, 0, nullptr);
    if (FAILED(hr)) return hr;

    hr = ::SHBindToObject(nullptr, target, pbc, riid, ppv);
    ::CoTaskMemFree(target);
    return hr;
}

IFACEMETHODIMP RootFolder::BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid,
                                         void** ppv) {
    return BindToObject(pidl, pbc, riid, ppv);
}

IFACEMETHODIMP RootFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1,
                                      PCUIDLIST_RELATIVE pidl2) {
    (void)lParam;
    // Order by position in state.json so the newest folder stays on top instead
    // of the shell sorting our list alphabetically.
    const USHORT a = ChildIndex(reinterpret_cast<PCUITEMID_CHILD>(pidl1));
    const USHORT b = ChildIndex(reinterpret_cast<PCUITEMID_CHILD>(pidl2));
    const short result = a < b ? -1 : (a > b ? 1 : 0);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<USHORT>(result));
}

IFACEMETHODIMP RootFolder::CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    (void)hwndOwner;

    if (riid == IID_IShellView) {
        SFV_CREATE info{};
        info.cbSize = sizeof(info);
        info.pshf = static_cast<IShellFolder*>(this);
        IShellView* view = nullptr;
        const HRESULT hr = ::SHCreateShellFolderView(&info, &view);
        if (FAILED(hr)) return hr;
        *ppv = view;
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP RootFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                           SFGAOF* rgfInOut) {
    if (!rgfInOut) return E_POINTER;
    if (cidl == 0 || !apidl) {
        *rgfInOut &= SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE;
        return S_OK;
    }

    SFGAOF common = kChildAttributes;
    for (UINT i = 0; i < cidl; ++i) {
        if (!AsChildItem(apidl[i])) return E_INVALIDARG;
    }
    *rgfInOut &= common;
    return S_OK;
}

IFACEMETHODIMP RootFolder::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                         REFIID riid, UINT* rgfReserved, void** ppv) {
    (void)hwndOwner;
    (void)rgfReserved;
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (cidl != 1 || !apidl) return E_INVALIDARG;

    const std::wstring path = ChildPath(apidl[0]);
    if (path.empty()) return E_INVALIDARG;

    if (riid == IID_IExtractIconW) {
        try {
            auto* icon = new (std::nothrow) FolderIcon(path);
            if (!icon) return E_OUTOFMEMORY;
            *ppv = static_cast<IExtractIconW*>(icon);
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    // Context menus, drag/drop and property sheets are deliberately not offered:
    // every one of them is another way to crash the host process.
    return E_NOINTERFACE;
}

IFACEMETHODIMP RootFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET* pName) {
    if (!pName) return E_POINTER;

    const ChildItem* item = AsChildItem(pidl);
    if (!item) return E_INVALIDARG;

    try {
        const std::wstring path(item->path);
        // FORPARSING without INFOLDER must yield something the shell can parse
        // back into the real folder -- that is the whole trick of this extension.
        const bool wantsPath =
            (uFlags & SHGDN_FORPARSING) != 0 && (uFlags & SHGDN_INFOLDER) == 0;
        return StringToStrRet(wantsPath ? path : LeafName(path), pName);
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

IFACEMETHODIMP RootFolder::SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, PCWSTR pszName,
                                     SHGDNF uFlags, PITEMID_CHILD* ppidlOut) {
    (void)hwnd;
    (void)pidl;
    (void)pszName;
    (void)uFlags;
    if (ppidlOut) *ppidlOut = nullptr;
    return E_NOTIMPL;  // the list is generated, renaming makes no sense
}

// --- IShellFolder2 ---------------------------------------------------------

IFACEMETHODIMP RootFolder::GetDefaultSearchGUID(GUID* pguid) {
    (void)pguid;
    return E_NOTIMPL;
}

IFACEMETHODIMP RootFolder::EnumSearches(IEnumExtraSearch** ppenum) {
    if (ppenum) *ppenum = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP RootFolder::GetDefaultColumn(DWORD dwRes, ULONG* pSort, ULONG* pDisplay) {
    (void)dwRes;
    if (pSort) *pSort = 0;
    if (pDisplay) *pDisplay = 0;
    return S_OK;
}

IFACEMETHODIMP RootFolder::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pcsFlags) {
    if (!pcsFlags) return E_POINTER;
    if (iColumn > 1) return E_INVALIDARG;
    *pcsFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT;
    return S_OK;
}

IFACEMETHODIMP RootFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID* pscid,
                                        VARIANT* pv) {
    (void)pidl;
    (void)pscid;
    (void)pv;
    return E_NOTIMPL;
}

IFACEMETHODIMP RootFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS* psd) {
    if (!psd) return E_POINTER;
    if (iColumn > 1) return E_INVALIDARG;

    psd->fmt = LVCFMT_LEFT;
    psd->cxChar = iColumn == 0 ? 30 : 60;

    // A null PIDL asks for the column heading.
    if (!pidl) {
        return StringToStrRet(iColumn == 0 ? L"Name" : L"Path", &psd->str);
    }

    const ChildItem* item = AsChildItem(pidl);
    if (!item) return E_INVALIDARG;
    try {
        const std::wstring path(item->path);
        return StringToStrRet(iColumn == 0 ? LeafName(path) : path, &psd->str);
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

IFACEMETHODIMP RootFolder::MapColumnToSCID(UINT iColumn, SHCOLUMNID* pscid) {
    (void)iColumn;
    (void)pscid;
    return E_NOTIMPL;
}

}  // namespace lfs
