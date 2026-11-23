#ifndef PLUGIN_SYSTEM_CPP_DYNAMIC_LIBRARY_HPP
#define PLUGIN_SYSTEM_CPP_DYNAMIC_LIBRARY_HPP

#include <optional>
#include <string>

namespace plugin_host {

// RAII wrapper around a platform dynamic library handle
// (dlopen/dlsym/dlclose on POSIX, LoadLibrary/GetProcAddress/FreeLibrary
// on Windows). Never throws; all fallible operations report failure via
// return value, matching the no-exceptions-across-the-ABI discipline
// used everywhere else in this host (even though this class itself is
// host-only code and technically could throw, keeping it exception-free
// keeps one consistent error-handling style across the whole loader).
class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;

    DynamicLibrary(DynamicLibrary &&other) noexcept;
    DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;

    // Attempts to load the shared library at `path`. On failure, returns
    // false and `last_error()` describes why; the object remains
    // unloaded (is_loaded() == false).
    bool load(const std::string &path);

    // Resolves a symbol by name. Returns nullptr if not loaded or the
    // symbol is missing.
    void *resolve_symbol(const std::string &symbol_name) const;

    bool is_loaded() const { return handle_ != nullptr; }

    const std::string &path() const { return path_; }
    const std::string &last_error() const { return last_error_; }

    // Explicitly unloads early (normally happens in the destructor).
    // Safe to call multiple times.
    void unload();

private:
    void *handle_ = nullptr;
    std::string path_;
    mutable std::string last_error_;
};

}  // namespace plugin_host

#endif  // PLUGIN_SYSTEM_CPP_DYNAMIC_LIBRARY_HPP
