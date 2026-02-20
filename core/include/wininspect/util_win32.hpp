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

class SafeHandle {
public:
    SafeHandle(HANDLE h = INVALID_HANDLE_VALUE) : h_(h) {}
    ~SafeHandle() { if (h_ != INVALID_HANDLE_VALUE && h_ != NULL) CloseHandle(h_); }
    SafeHandle(SafeHandle&& other) noexcept : h_(other.h_) { other.h_ = INVALID_HANDLE_VALUE; }
    SafeHandle& operator=(SafeHandle&& other) noexcept {
        if (this != static_cast<const void*>(&other)) {
            if (h_ != INVALID_HANDLE_VALUE && h_ != NULL) CloseHandle(h_);
            h_ = other.h_;
            other.h_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    operator HANDLE() const { return h_; }
    HANDLE get() const { return h_; }
    bool is_valid() const { return h_ != INVALID_HANDLE_VALUE && h_ != NULL; }
private:
    HANDLE h_;
    SafeHandle(const SafeHandle&) = delete;
    SafeHandle& operator=(const SafeHandle&) = delete;
};

class HKey {
public:
    HKey(HKEY h = NULL) : h_(h) {}
    ~HKey() { if (h_ != NULL && h_ != HKEY_LOCAL_MACHINE && h_ != HKEY_CURRENT_USER && h_ != HKEY_CLASSES_ROOT && h_ != HKEY_USERS) RegCloseKey(h_); }
    HKey(HKey&& other) noexcept : h_(other.h_) { other.h_ = NULL; }
    HKey& operator=(HKey&& other) noexcept {
        if (this != static_cast<const void*>(&other)) {
            if (h_ != NULL && h_ != HKEY_LOCAL_MACHINE && h_ != HKEY_CURRENT_USER && h_ != HKEY_CLASSES_ROOT && h_ != HKEY_USERS) RegCloseKey(h_);
            h_ = other.h_;
            other.h_ = NULL;
        }
        return *this;
    }
    operator HKEY() const { return h_; }
    HKEY* operator&() { return &h_; }
    bool is_valid() const { return h_ != NULL; }
private:
    HKEY h_;
    HKey(const HKey&) = delete;
    HKey& operator=(const HKey&) = delete;
};

class ScHandle {
public:
    ScHandle(SC_HANDLE h = NULL) : h_(h) {}
    ~ScHandle() { if (h_ != NULL) CloseServiceHandle(h_); }
    ScHandle(ScHandle&& other) noexcept : h_(other.h_) { other.h_ = NULL; }
    ScHandle& operator=(ScHandle&& other) noexcept {
        if (this != static_cast<const void*>(&other)) {
            if (h_ != NULL) CloseServiceHandle(h_);
            h_ = other.h_;
            other.h_ = NULL;
        }
        return *this;
    }
    operator SC_HANDLE() const { return h_; }
    bool is_valid() const { return h_ != NULL; }
private:
    SC_HANDLE h_;
    ScHandle(const ScHandle&) = delete;
    ScHandle& operator=(const ScHandle&) = delete;
};

} // namespace wininspect
#endif
