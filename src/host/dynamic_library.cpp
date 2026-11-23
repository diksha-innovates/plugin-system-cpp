#include "dynamic_library.hpp"

#include <utility>

#if defined(_WIN32)
// ---------------------------------------------------------------------
// Windows code path. Written to document the intended portable design
// but NEVER compiled or tested on this machine (macOS). Treat it as
// unverified until it has actually been built and exercised on Windows.
// ---------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace plugin_host {

DynamicLibrary::~DynamicLibrary() { unload(); }

DynamicLibrary::DynamicLibrary(DynamicLibrary &&other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_)), last_error_(std::move(other.last_error_)) {
    other.handle_ = nullptr;
}

DynamicLibrary &DynamicLibrary::operator=(DynamicLibrary &&other) noexcept {
    if (this != &other) {
        unload();
        handle_ = other.handle_;
        path_ = std::move(other.path_);
        last_error_ = std::move(other.last_error_);
        other.handle_ = nullptr;
    }
    return *this;
}

bool DynamicLibrary::load(const std::string &path) {
    unload();

#if defined(_WIN32)
    HMODULE module = ::LoadLibraryA(path.c_str());
    if (module == nullptr) {
        DWORD err = ::GetLastError();
        last_error_ = "LoadLibraryA failed for '" + path + "' (GetLastError=" + std::to_string(err) + ")";
        return false;
    }
    handle_ = static_cast<void *>(module);
    path_ = path;
    return true;
#else
    // RTLD_NOW: resolve all symbols immediately rather than lazily
    // (RTLD_LAZY). This is deliberate: we want any missing-symbol
    // problem inside the plugin binary itself (e.g. it references a
    // libc++ symbol that isn't available) to surface at load() time,
    // in a single place we already handle failures for, rather than
    // nondeterministically later the first time some rarely-used
    // internal function is first called. For a host that loads
    // untrusted/third-party plugins, failing fast and in one
    // predictable spot is worth the (tiny, one-time, per-plugin) extra
    // load cost of RTLD_NOW over RTLD_LAZY.
    void *h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) {
        const char *err = ::dlerror();
        last_error_ = "dlopen failed for '" + path + "': " + (err != nullptr ? err : "unknown error");
        return false;
    }
    handle_ = h;
    path_ = path;
    return true;
#endif
}

void *DynamicLibrary::resolve_symbol(const std::string &symbol_name) const {
    if (handle_ == nullptr) {
        last_error_ = "resolve_symbol called on an unloaded library";
        return nullptr;
    }
#if defined(_WIN32)
    FARPROC proc = ::GetProcAddress(static_cast<HMODULE>(handle_), symbol_name.c_str());
    if (proc == nullptr) {
        last_error_ = "GetProcAddress could not find symbol '" + symbol_name + "'";
        return nullptr;
    }
    return reinterpret_cast<void *>(proc);
#else
    // dlerror() must be cleared first: dlsym() can legitimately return
    // nullptr for a symbol whose real value is nullptr, so nullptr alone
    // does not mean "not found" - checking dlerror() afterwards does.
    ::dlerror();
    void *sym = ::dlsym(handle_, symbol_name.c_str());
    const char *err = ::dlerror();
    if (err != nullptr) {
        last_error_ = "dlsym could not find symbol '" + symbol_name + "': " + err;
        return nullptr;
    }
    return sym;
#endif
}

void DynamicLibrary::unload() {
    if (handle_ == nullptr) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle_));
#else
    ::dlclose(handle_);
#endif
    handle_ = nullptr;
}

}  // namespace plugin_host
