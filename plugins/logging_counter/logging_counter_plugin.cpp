// Example plugin: keeps per-instance state (a call counter) across
// multiple execute() calls, demonstrating that plugin_instance can carry
// real plugin-private data - the host only ever sees it as an opaque
// pointer. Every execute() call appends a line to the process-wide log
// and returns the running count for this instance.

#include "plugin_abi.h"

#include <new>
#include <stdio.h>
#include <string.h>

struct plugin_instance {
    long call_count;
};

extern "C" {

static plugin_status logging_counter_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
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

static plugin_status logging_counter_execute(plugin_instance *instance, const char *input, char *output_buffer,
                                              size_t output_buffer_size, size_t *out_written) {
    if (instance == nullptr || input == nullptr || output_buffer == nullptr || output_buffer_size == 0) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    instance->call_count += 1;

    int needed = snprintf(output_buffer, output_buffer_size, "count=%ld last_input=%s", instance->call_count, input);
    if (needed < 0 || static_cast<size_t>(needed) >= output_buffer_size) {
        return PLUGIN_STATUS_BUFFER_TOO_SMALL;
    }
    if (out_written != nullptr) {
        *out_written = static_cast<size_t>(needed);
    }
    return PLUGIN_STATUS_OK;
}

static plugin_status logging_counter_shutdown(plugin_instance *instance) {
    delete instance;
    return PLUGIN_STATUS_OK;
}

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)),
    "logging_counter",
    "1.0.0",
    {logging_counter_init, logging_counter_execute, logging_counter_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
