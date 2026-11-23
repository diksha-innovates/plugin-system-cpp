// TEST FIXTURE - not a real plugin.
//
// Exports plugin_get_metadata() with an abi_version that does not match
// PLUGIN_ABI_VERSION (and, separately from that, a struct_size field
// that also does not match sizeof(plugin_metadata) on the host). Used
// to verify the host detects and rejects an ABI mismatch cleanly,
// without calling a single vtable function, before touching anything
// else in the struct.

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
    PLUGIN_ABI_VERSION + 777u,  // deliberately wrong ABI version
    static_cast<uint32_t>(sizeof(plugin_metadata)) + 8u,  // deliberately wrong struct size too
    "bad_abi_version_fixture",
    "0.0.1",
    {fail_init, fail_execute, fail_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
