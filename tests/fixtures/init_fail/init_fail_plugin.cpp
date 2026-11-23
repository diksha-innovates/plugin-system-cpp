// TEST FIXTURE - not a real plugin.
//
// Exports valid, ABI-correct metadata, but init() always deliberately
// returns a failure status. Used to verify the host survives an init
// failure cleanly: no crash, no leaked/partially-initialized plugin
// instance, and the shared library gets unloaded again.

#include "plugin_abi.h"

extern "C" {

static plugin_status init_fail_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
    (void)host_ctx;
    // Contractually: on failure we must NOT write to *out_instance and
    // must not have allocated anything that needs freeing.
    (void)out_instance;
    return PLUGIN_STATUS_ERROR;
}

static plugin_status init_fail_execute(plugin_instance *instance, const char *input, char *output_buffer,
                                        size_t output_buffer_size, size_t *out_written) {
    (void)instance;
    (void)input;
    (void)output_buffer;
    (void)output_buffer_size;
    (void)out_written;
    return PLUGIN_STATUS_NOT_INITIALIZED;
}

static plugin_status init_fail_shutdown(plugin_instance *instance) {
    // Must never be called by the host for this fixture, since init()
    // never succeeds - tests assert on that.
    (void)instance;
    return PLUGIN_STATUS_OK;
}

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)),
    "init_fail_fixture",
    "0.0.1",
    {init_fail_init, init_fail_execute, init_fail_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
