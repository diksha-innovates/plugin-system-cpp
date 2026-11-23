#include "plugin_manager.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>

namespace plugin_host {

namespace {

#if defined(_WIN32)
constexpr const char *kPluginExtension = ".dll";
#elif defined(__APPLE__)
constexpr const char *kPluginExtension = ".dylib";
#else
constexpr const char *kPluginExtension = ".so";
#endif

std::string make_metadata_mismatch_message(const char *what, std::uint32_t expected, std::uint32_t actual) {
    return std::string(what) + ": host expects " + std::to_string(expected) + ", plugin reports " +
           std::to_string(actual);
}

}  // namespace

std::string to_string(LoadError error) {
    switch (error) {
        case LoadError::None:
            return "None";
        case LoadError::FileNotFound:
            return "FileNotFound";
        case LoadError::OpenFailed:
            return "OpenFailed";
        case LoadError::MissingMetadataSymbol:
            return "MissingMetadataSymbol";
        case LoadError::NullMetadata:
            return "NullMetadata";
        case LoadError::AbiVersionMismatch:
            return "AbiVersionMismatch";
        case LoadError::StructSizeMismatch:
            return "StructSizeMismatch";
        case LoadError::MissingVtableFunction:
            return "MissingVtableFunction";
        case LoadError::InitFailed:
            return "InitFailed";
        case LoadError::AlreadyAtCapacity:
            return "AlreadyAtCapacity";
    }
    return "Unknown";
}

// --------------------------------------------------------------------
// LoadedPlugin
// --------------------------------------------------------------------

LoadedPlugin::LoadedPlugin(DynamicLibrary library, const plugin_metadata *metadata, plugin_instance *instance)
    : library_(std::move(library)),
      metadata_(metadata),
      instance_(instance),
      name_(metadata->name),
      version_(metadata->version),
      state_(PluginLifecycleState::Initialized) {}

plugin_status LoadedPlugin::execute(const char *input, char *output_buffer, std::size_t output_buffer_size,
                                     std::size_t *out_written) {
    if (state_ != PluginLifecycleState::Initialized) {
        return PLUGIN_STATUS_NOT_INITIALIZED;
    }
    if (metadata_->vtable.execute == nullptr) {
        return PLUGIN_STATUS_UNSUPPORTED_OPERATION;
    }
    return metadata_->vtable.execute(instance_, input, output_buffer, output_buffer_size, out_written);
}

void *LoadedPlugin::resolve_extra_symbol(const std::string &symbol_name) const {
    return library_.resolve_symbol(symbol_name);
}

plugin_status LoadedPlugin::shutdown_once() {
    if (state_ == PluginLifecycleState::ShutDown) {
        // Already shut down: this is the guard that guarantees the
        // plugin's shutdown() is called exactly once no matter how many
        // times host code calls shutdown_once().
        return PLUGIN_STATUS_OK;
    }
    plugin_status status = PLUGIN_STATUS_OK;
    if (metadata_->vtable.shutdown != nullptr) {
        status = metadata_->vtable.shutdown(instance_);
    }
    instance_ = nullptr;
    state_ = PluginLifecycleState::ShutDown;
    return status;
}

// --------------------------------------------------------------------
// PluginManager
// --------------------------------------------------------------------

PluginManager::~PluginManager() { unload_all(); }

LoadResult PluginManager::load(const std::string &path) {
    LoadResult result;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        result.error = LoadError::FileNotFound;
        result.message = "file not found: " + path;
        return result;
    }

    DynamicLibrary library;
    if (!library.load(path)) {
        result.error = LoadError::OpenFailed;
        result.message = library.last_error();
        return result;
    }

    auto get_metadata =
        reinterpret_cast<plugin_get_metadata_fn>(library.resolve_symbol(PLUGIN_GET_METADATA_SYMBOL));
    if (get_metadata == nullptr) {
        result.error = LoadError::MissingMetadataSymbol;
        result.message = "plugin does not export '" + std::string(PLUGIN_GET_METADATA_SYMBOL) + "': " +
                          library.last_error();
        // library (RAII) unloads itself when it goes out of scope.
        return result;
    }

    const plugin_metadata *metadata = get_metadata();
    if (metadata == nullptr) {
        result.error = LoadError::NullMetadata;
        result.message = "plugin_get_metadata() returned null";
        return result;
    }

    if (metadata->abi_version != PLUGIN_ABI_VERSION) {
        result.error = LoadError::AbiVersionMismatch;
        result.message = make_metadata_mismatch_message("ABI version mismatch", PLUGIN_ABI_VERSION,
                                                          metadata->abi_version);
        return result;
    }

    if (metadata->struct_size != sizeof(plugin_metadata)) {
        result.error = LoadError::StructSizeMismatch;
        result.message = make_metadata_mismatch_message("metadata struct size mismatch",
                                                          static_cast<std::uint32_t>(sizeof(plugin_metadata)),
                                                          metadata->struct_size);
        return result;
    }

    // From this point on the struct layout is trusted: name/version are
    // NUL-terminated fixed-size buffers we can safely copy out of.
    result.plugin_name.assign(metadata->name, strnlen(metadata->name, PLUGIN_NAME_MAX_LEN));

    if (metadata->vtable.init == nullptr || metadata->vtable.shutdown == nullptr) {
        result.error = LoadError::MissingVtableFunction;
        result.message = "plugin metadata vtable is missing a required function pointer";
        return result;
    }

    plugin_instance *instance = nullptr;
    plugin_status init_status = metadata->vtable.init(&instance, nullptr);
    if (init_status != PLUGIN_STATUS_OK || instance == nullptr) {
        result.error = LoadError::InitFailed;
        result.message = "plugin init() failed with status " + std::to_string(static_cast<int>(init_status));
        // The plugin is contractually required to not leak on a failed
        // init, so there is nothing for the host to clean up on the
        // plugin side. The DynamicLibrary (RAII) still unloads the
        // shared library itself when `library` goes out of scope below.
        return result;
    }

    auto loaded = std::make_unique<LoadedPlugin>(std::move(library), metadata, instance);
    plugins_.push_back(std::move(loaded));

    result.success = true;
    result.error = LoadError::None;
    result.index = plugins_.size() - 1;
    return result;
}

std::vector<LoadResult> PluginManager::load_directory(const std::string &directory) {
    std::vector<LoadResult> results;

    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) {
        LoadResult r;
        r.error = LoadError::FileNotFound;
        r.message = "cannot open directory: " + directory;
        results.push_back(std::move(r));
        return results;
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        if (entry.path().extension() == kPluginExtension) {
            candidates.push_back(entry.path());
        }
    }
    std::sort(candidates.begin(), candidates.end());

    for (const auto &path : candidates) {
        results.push_back(load(path.string()));
    }
    return results;
}

void PluginManager::unload_all() {
    // Strict reverse order of load: pop from the back of the vector,
    // which is exactly the LIFO order plugins were pushed in by load().
    while (!plugins_.empty()) {
        plugins_.back()->shutdown_once();
        plugins_.pop_back();  // destroys LoadedPlugin -> DynamicLibrary dtor -> dlclose
    }
}

}  // namespace plugin_host
