// IEnumIDList over a snapshot of state.json.
//
// The snapshot is taken when the enumerator is created, so a state.json rewrite
// halfway through an enumeration cannot change the list under the shell's feet.
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <vector>

#include "state_reader.h"

namespace lfs {

class ChildEnumerator : public IEnumIDList {
public:
    explicit ChildEnumerator(std::vector<StateFolder> folders, size_t start = 0);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IEnumIDList
    IFACEMETHODIMP Next(ULONG celt, PITEMID_CHILD* rgelt, ULONG* pceltFetched) override;
    IFACEMETHODIMP Skip(ULONG celt) override;
    IFACEMETHODIMP Reset() override;
    IFACEMETHODIMP Clone(IEnumIDList** ppenum) override;

private:
    ~ChildEnumerator();

    long refCount_ = 1;
    std::vector<StateFolder> folders_;
    size_t position_ = 0;
};

}  // namespace lfs
