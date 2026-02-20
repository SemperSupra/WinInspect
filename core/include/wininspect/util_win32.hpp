#pragma once
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>

namespace wininspect {

template <typename T>
class ComPtr {
public:
    ComPtr() : ptr_(nullptr) {}
    ComPtr(T* p) : ptr_(p) { if (ptr_) ptr_->AddRef(); }
    ~ComPtr() { if (ptr_) ptr_->Release(); }

    ComPtr(const ComPtr& other) : ptr_(other.ptr_) { if (ptr_) ptr_->AddRef(); }
    ComPtr& operator=(const ComPtr& other) {
        if (this != &other) {
            if (ptr_) ptr_->Release();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->AddRef();
        }
        return *this;
    }

    T* operator->() const { return ptr_; }
    T** operator&() { return &ptr_; }
    operator T*() const { return ptr_; }
    T* get() const { return ptr_; }

    bool operator!() const { return ptr_ == nullptr; }

private:
    T* ptr_;
};

struct CoInitGuard {
    HRESULT hr;
    CoInitGuard(DWORD dwCoInit = COINIT_MULTITHREADED) {
        hr = CoInitializeEx(NULL, dwCoInit);
    }
    ~CoInitGuard() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

} // namespace wininspect
#endif
