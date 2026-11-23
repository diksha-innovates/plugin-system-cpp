// TEST FIXTURE - not a real plugin.
//
// Built three times under different names (order_tracker_a/b/c, see
// CMakeLists.txt, each with a different -DORDER_TRACKER_NAME) from this
// single source file. Each instance appends "init:<name>" /
// "shutdown:<name>" lines to the file named by the PLUGIN_ORDER_LOG_PATH
// environment variable, from inside the plugin's own init()/shutdown()
// - not from host-side bookkeeping - so the recorded order genuinely
// reflects what the plugin itself observed, which is what the reverse-
// unload-order test checks.

#include "plugin_abi.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>

#ifndef ORDER_TRACKER_NAME
#define ORDER_TRACKER_NAME "order_tracker_unknown"
#endif

namespace {

void append_log_line(const char *event) {
    const char *path = getenv("PLUGIN_ORDER_LOG_PATH");
    if (path == nullptr) {
        return;
    }
    FILE *f = fopen(path, "a");
    if (f == nullptr) {
        return;
    }
    fprintf(f, "%s:%s\n", event, ORDER_TRACKER_NAME);
    fclose(f);
}

}  // namespace

struct plugin_instance {
    int dummy;
};

extern "C" {

static plugin_status order_tracker_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
    (void)host_ctx;
    if (out_instance == nullptr) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    plugin_instance *inst = new (std::nothrow) plugin_instance{0};
    if (inst == nullptr) {
        return PLUGIN_STATUS_ERROR;
    }
    append_log_line("init");
    *out_instance = inst;
    return PLUGIN_STATUS_OK;
}

static plugin_status order_tracker_execute(plugin_instance *instance, const char *input, char *output_buffer,
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

static plugin_status order_tracker_shutdown(plugin_instance *instance) {
    append_log_line("shutdown");
    delete instance;
    return PLUGIN_STATUS_OK;
}

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)),
    ORDER_TRACKER_NAME,
    "0.0.1",
    {order_tracker_init, order_tracker_execute, order_tracker_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
