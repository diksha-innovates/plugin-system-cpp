#ifndef PLUGIN_SYSTEM_CPP_PLUGIN_MANAGER_HPP
#define PLUGIN_SYSTEM_CPP_PLUGIN_MANAGER_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "dynamic_library.hpp"
#include "plugin_abi.h"

namespace plugin_host {

// Why a plugin can fail to become usable, reported to the caller instead
// of being thrown. Every value here corresponds to a failure mode the
// spec calls out explicitly: missing file, missing symbol, ABI/struct
// mismatch, and init failure.
enum class LoadError {
    None,
    FileNotFound,
    OpenFailed,
    MissingMetadataSymbol,
    NullMetadata,
    AbiVersionMismatch,
    StructSizeMismatch,
    MissingVtableFunction,
    InitFailed,
    AlreadyAtCapacity,
};

std::string to_string(LoadError error);

struct LoadResult {
    bool success = false;
    LoadError error = LoadError::None;
    std::string message;   // human-readable detail, always set on failure
    std::string plugin_name;  // filled in if metadata was readable, even on init failure
    std::size_t index = 0;  // valid only if success
};

// Lifecycle state of one loaded plugin, tracked by the host so that
// init()/shutdown() are each invoked at most once no matter how the
// manager's methods are called.
enum class PluginLifecycleState {
    Initialized,  // init() succeeded, execute() may be called
    ShutDown,     // shutdown() has already run; instance pointer is dangling
};

// One successfully loaded, successfully initialized plugin. Owns the
// shared-library handle (RAII) and the opaque instance pointer the
// plugin gave us. Never copyable - there is exactly one owner for a
// given OS library handle and a given instance pointer.
class LoadedPlugin {
public:
    LoadedPlugin(DynamicLibrary library, const plugin_metadata *metadata, plugin_instance *instance);

    LoadedPlugin(const LoadedPlugin &) = delete;
    LoadedPlugin &operator=(const LoadedPlugin &) = delete;
    LoadedPlugin(LoadedPlugin &&) = default;
    LoadedPlugin &operator=(LoadedPlugin &&) = default;

    const std::string &name() const { return name_; }
    const std::string &version() const { return version_; }
    const std::string &path() const { return library_.path(); }
    PluginLifecycleState state() const { return state_; }

    // Calls the plugin's execute(); fails cleanly (no crash) if the
    // plugin has already been shut down.
    plugin_status execute(const char *input, char *output_buffer, std::size_t output_buffer_size,
                           std::size_t *out_written);

    // Resolves an additional, optional symbol beyond the core vtable
    // (e.g. a plugin-specific debug/diagnostic export). Returns
    // nullptr if the plugin does not export it. Exposed mainly so
    // tests can verify fixture-internal instrumentation (like a
    // shutdown call counter) without the host's core ABI needing to
    // know about it.
    void *resolve_extra_symbol(const std::string &symbol_name) const;

    // Idempotent: calls shutdown() on the plugin exactly once, the
    // first time this is called for this instance; every subsequent
    // call is a harmless no-op that returns PLUGIN_STATUS_OK without
    // touching the plugin again. This is the mechanism that guarantees
    // "cleanup happens exactly once" even if host code calls it more
    // than once.
    plugin_status shutdown_once();

private:
    DynamicLibrary library_;
    const plugin_metadata *metadata_;
    plugin_instance *instance_;
    std::string name_;
    std::string version_;
    PluginLifecycleState state_;
};

// Owns a stack of loaded plugins and enforces the host-owned lifecycle
// contract documented in the README:
//   - init() is called exactly once, right after a successful load +
//     ABI validation.
//   - shutdown() is called exactly once, before the library is unloaded.
//   - plugins are unloaded in strict reverse order of load (a stack),
//     matching the usual shared-library/dependency-safety convention
//     (the most recently loaded plugin is the most likely to depend on
//     something set up after an earlier one, so it must be torn down
//     first - the example plugins here don't actually depend on each
//     other, but a host that didn't enforce this ordering would be
//     unsafe to extend with plugins that do).
class PluginManager {
public:
    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager &) = delete;
    PluginManager &operator=(const PluginManager &) = delete;

    // Loads one plugin file: opens the library, validates ABI version
    // and struct size, resolves the vtable, and calls init() exactly
    // once. On ANY failure the library is unloaded again before
    // returning and nothing is left behind (no partially-initialized
    // plugin, no leaked OS handle) - the host survives and can keep
    // going.
    LoadResult load(const std::string &path);

    // Loads every regular file in `directory` that looks like a shared
    // library for this platform (.dylib on macOS, .so on Linux/other
    // POSIX, .dll on Windows). Returns one LoadResult per file
    // attempted (not just the successes) so the caller can report a
    // full per-file summary. A bad plugin never stops the scan.
    std::vector<LoadResult> load_directory(const std::string &directory);

    std::size_t loaded_count() const { return plugins_.size(); }
    const LoadedPlugin &plugin_at(std::size_t index) const { return *plugins_.at(index); }
    LoadedPlugin &plugin_at(std::size_t index) { return *plugins_.at(index); }

    // Shuts down and unloads every currently loaded plugin, in strict
    // reverse order of load. Safe to call multiple times (a no-op once
    // the stack is empty). Also called automatically by the destructor.
    void unload_all();

private:
    std::vector<std::unique_ptr<LoadedPlugin>> plugins_;  // index 0 = first loaded
};

}  // namespace plugin_host

#endif  // PLUGIN_SYSTEM_CPP_PLUGIN_MANAGER_HPP
