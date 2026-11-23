// TEST FIXTURE - not a real plugin.
//
// Tracks how many times shutdown() has actually been invoked, via a
// process-wide (translation-unit-static) counter, and exports an extra
// diagnostic symbol (fixture_get_shutdown_count) so tests can read it
// directly. Used to verify the host's "cleanup happens exactly once"
// guarantee: even if host code calls the manager's shutdown path more
// than once for this plugin, the counter here must never exceed 1.

#include "plugin_abi.h"

#include <new>

namespace {
int g_shutdown_count = 0;
}  // namespace

struct plugin_instance {
    int dummy;
};

extern "C" {

static plugin_status counter_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
    (void)host_ctx;
    if (out_instance == nullptr) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    plugin_instance *inst = new (std::nothrow) plugin_instance{0};
    if (inst == nullptr) {
        return PLUGIN_STATUS_ERROR;
    }
    *out_instance = inst;
    return PLUGIN_STATUS_OK;
}

static plugin_status counter_execute(plugin_instance *instance, const char *input, char *output_buffer,
                                      size_t output_buffer_size, size_t *out_written) {
    (void)instance;
    (void)input;
    if (output_buffer == nullptr || output_buffer_size == 0) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    output_buffer[0] = '\0';
    if (out_written != nullptr) {
        *out_written = 0;
    }
    return PLUGIN_STATUS_OK;
}

static plugin_status counter_shutdown(plugin_instance *instance) {
    g_shutdown_count += 1;
    delete instance;
    return PLUGIN_STATUS_OK;
}

// Extra diagnostic export, not part of the core ABI - tests resolve it
// via PluginManager::resolve_extra_symbol / LoadedPlugin::resolve_extra_symbol.
int fixture_get_shutdown_count(void) { return g_shutdown_count; }

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)),
    "shutdown_counter_fixture",
    "0.0.1",
    {counter_init, counter_execute, counter_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
