// The "Last Folder Standing" node itself.
//
// Its children are the folders from state.json. A child reports itself as a
// filesystem folder whose parsing name is the real target path, which is what
// makes a click navigate to that folder instead of into some virtual view.
#pragma once

#include <windows.h>
#include <shlobj.h>

namespace lfs {

// What the node itself claims to be. Registered in the registry and reported
// again at runtime, so both have to say the same thing.
//
// SFGAO_FILESYSANCESTOR is not cosmetic. A file dialog that wants real paths
// sets FOS_FORCEFILESYSTEM and then hides every navigation pane node that is
// neither a filesystem object nor able to contain one. Adobe's dialogs do this;
// without the flag the node is visible in Explorer but missing there.
constexpr SFGAOF kRootAttributes =
    SFGAO_FOLDER | SFGAO_FILESYSANCESTOR | SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE;

class RootFolder : public IShellFolder2, public IPersistFolder2 {
public:
    RootFolder();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IPersist / IPersistFolder / IPersistFolder2
    IFACEMETHODIMP GetClassID(CLSID* pClassID) override;
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl) override;
    IFACEMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE* ppidl) override;

    // IShellFolder
    IFACEMETHODIMP ParseDisplayName(HWND hwnd, IBindCtx* pbc, PWSTR pszDisplayName,
                                    ULONG* pchEaten, PIDLIST_RELATIVE* ppidl,
                                    ULONG* pdwAttributes) override;
    IFACEMETHODIMP EnumObjects(HWND hwnd, SHCONTF grfFlags, IEnumIDList** ppenumIDList) override;
    IFACEMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid,
                                void** ppv) override;
    IFACEMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid,
                                 void** ppv) override;
    IFACEMETHODIMP CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1,
                              PCUIDLIST_RELATIVE pidl2) override;
    IFACEMETHODIMP CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) override;
    IFACEMETHODIMP GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                   SFGAOF* rgfInOut) override;
    IFACEMETHODIMP GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                 REFIID riid, UINT* rgfReserved, void** ppv) override;
    IFACEMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags,
                                    STRRET* pName) override;
    IFACEMETHODIMP SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, PCWSTR pszName, SHGDNF uFlags,
                             PITEMID_CHILD* ppidlOut) override;

    // IShellFolder2
    IFACEMETHODIMP GetDefaultSearchGUID(GUID* pguid) override;
    IFACEMETHODIMP EnumSearches(IEnumExtraSearch** ppenum) override;
    IFACEMETHODIMP GetDefaultColumn(DWORD dwRes, ULONG* pSort, ULONG* pDisplay) override;
    IFACEMETHODIMP GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pcsFlags) override;
    IFACEMETHODIMP GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID* pscid,
                                VARIANT* pv) override;
    IFACEMETHODIMP GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn,
                                SHELLDETAILS* psd) override;
    IFACEMETHODIMP MapColumnToSCID(UINT iColumn, SHCOLUMNID* pscid) override;

private:
    ~RootFolder();

    long refCount_ = 1;
    PIDLIST_ABSOLUTE rootPidl_ = nullptr;
};

}  // namespace lfs
