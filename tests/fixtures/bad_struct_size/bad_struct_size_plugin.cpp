// TEST FIXTURE - not a real plugin.
//
// Reports the CORRECT abi_version but a WRONG struct_size, simulating a
// plugin that was built against a different (incompatible) layout of
// plugin_metadata while someone forgot to bump PLUGIN_ABI_VERSION. This
// is exactly the case abi_version alone cannot catch, which is why
// struct_size exists as a second, independent check.

#include "plugin_abi.h"

extern "C" {

static plugin_status fail_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
    (void)host_ctx;
    (void)out_instance;
    return PLUGIN_STATUS_ERROR;
}

static plugin_status fail_execute(plugin_instance *instance, const char *input, char *output_buffer,
                                   size_t output_buffer_size, size_t *out_written) {
    (void)instance;
    (void)input;
    (void)output_buffer;
    (void)output_buffer_size;
    (void)out_written;
    return PLUGIN_STATUS_ERROR;
}

static plugin_status fail_shutdown(plugin_instance *instance) {
    (void)instance;
    return PLUGIN_STATUS_ERROR;
}

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)) + 16u,  // deliberately wrong struct size
    "bad_struct_size_fixture",
    "0.0.1",
    {fail_init, fail_execute, fail_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
