#include "enum_children.h"

#include <new>

#include "dll.h"
#include "pidl.h"

namespace lfs {

ChildEnumerator::ChildEnumerator(std::vector<StateFolder> folders, size_t start)
    : folders_(std::move(folders)), position_(start) {
    DllAddRef();
}

ChildEnumerator::~ChildEnumerator() { DllRelease(); }

IFACEMETHODIMP ChildEnumerator::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumIDList) {
        *ppv = static_cast<IEnumIDList*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ChildEnumerator::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&refCount_));
}

IFACEMETHODIMP_(ULONG) ChildEnumerator::Release() {
    const long count = ::InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
}

IFACEMETHODIMP ChildEnumerator::Next(ULONG celt, PITEMID_CHILD* rgelt, ULONG* pceltFetched) {
    if (pceltFetched) *pceltFetched = 0;
    if (!rgelt || (celt != 1 && !pceltFetched)) return E_INVALIDARG;

    ULONG fetched = 0;
    while (fetched < celt && position_ < folders_.size()) {
        const StateFolder& folder = folders_[position_];
        PITEMID_CHILD pidl = CreateChildPidl(static_cast<USHORT>(position_), folder.path);
        ++position_;
        if (!pidl) continue;  // skip an unusable entry rather than failing the enum
        rgelt[fetched] = pidl;
        ++fetched;
    }

    if (pceltFetched) *pceltFetched = fetched;
    return fetched == celt ? S_OK : S_FALSE;
}

IFACEMETHODIMP ChildEnumerator::Skip(ULONG celt) {
    const size_t remaining = folders_.size() - position_;
    if (celt > remaining) {
        position_ = folders_.size();
        return S_FALSE;
    }
    position_ += celt;
    return S_OK;
}

IFACEMETHODIMP ChildEnumerator::Reset() {
    position_ = 0;
    return S_OK;
}

IFACEMETHODIMP ChildEnumerator::Clone(IEnumIDList** ppenum) {
    if (!ppenum) return E_POINTER;
    *ppenum = nullptr;
    try {
        auto* clone = new (std::nothrow) ChildEnumerator(folders_, position_);
        if (!clone) return E_OUTOFMEMORY;
        *ppenum = clone;
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

}  // namespace lfs
