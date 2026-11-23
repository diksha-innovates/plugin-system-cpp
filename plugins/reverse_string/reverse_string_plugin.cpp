// Example plugin: reverses the input string.
//
// Demonstrates the simplest possible plugin: no meaningful internal
// state is needed, so plugin_instance is defined as an empty struct
// just to have a valid, distinct, non-null pointer to hand back to the
// host.

#include "plugin_abi.h"

#include <new>
#include <string.h>

struct plugin_instance {
    int dummy;
};

extern "C" {

static plugin_status reverse_string_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
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

static plugin_status reverse_string_execute(plugin_instance *instance, const char *input, char *output_buffer,
                                             size_t output_buffer_size, size_t *out_written) {
    if (instance == nullptr || input == nullptr || output_buffer == nullptr || output_buffer_size == 0) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    size_t len = strlen(input);
    if (len + 1 > output_buffer_size) {
        return PLUGIN_STATUS_BUFFER_TOO_SMALL;
    }
    for (size_t i = 0; i < len; ++i) {
        output_buffer[i] = input[len - 1 - i];
    }
    output_buffer[len] = '\0';
    if (out_written != nullptr) {
        *out_written = len;
    }
    return PLUGIN_STATUS_OK;
}

static plugin_status reverse_string_shutdown(plugin_instance *instance) {
    delete instance;
    return PLUGIN_STATUS_OK;
}

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)),
    "reverse_string",
    "1.0.0",
    {reverse_string_init, reverse_string_execute, reverse_string_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
