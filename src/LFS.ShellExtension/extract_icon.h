// IExtractIconW for child items: hands back the icon location of the real
// folder without ever touching the folder itself.
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <string>

namespace lfs {

class FolderIcon : public IExtractIconW {
public:
    explicit FolderIcon(std::wstring targetPath);

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP GetIconLocation(UINT uFlags, PWSTR pszIconFile, UINT cchMax, int* piIndex,
                                   UINT* pwFlags) override;
    IFACEMETHODIMP Extract(PCWSTR pszFile, UINT nIconIndex, HICON* phiconLarge, HICON* phiconSmall,
                           UINT nIconSize) override;

private:
    ~FolderIcon();

    long refCount_ = 1;
    std::wstring path_;
};

}  // namespace lfs
