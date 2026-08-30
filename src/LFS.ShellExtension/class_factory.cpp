#include "class_factory.h"

#include <new>

#include "dll.h"
#include "root_folder.h"

namespace lfs {

ClassFactory::ClassFactory() { DllAddRef(); }

ClassFactory::~ClassFactory() { DllRelease(); }

IFACEMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ClassFactory::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&refCount_));
}

IFACEMETHODIMP_(ULONG) ClassFactory::Release() {
    const long count = ::InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
}

IFACEMETHODIMP ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    auto* folder = new (std::nothrow) RootFolder();
    if (!folder) return E_OUTOFMEMORY;

    const HRESULT hr = folder->QueryInterface(riid, ppv);
    folder->Release();
    return hr;
}

IFACEMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        DllAddRef();
    } else {
        DllRelease();
    }
    return S_OK;
}

}  // namespace lfs
