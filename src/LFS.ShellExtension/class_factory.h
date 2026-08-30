#pragma once

#include <windows.h>
#include <unknwn.h>

namespace lfs {

class ClassFactory : public IClassFactory {
public:
    ClassFactory();

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;

private:
    ~ClassFactory();

    long refCount_ = 1;
};

}  // namespace lfs
