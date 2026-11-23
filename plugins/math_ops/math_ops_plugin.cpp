// Example plugin: basic arithmetic. Input format is "op,a,b" where op
// is one of add/sub/mul/div, e.g. "add,3,4" -> "7.000000". Demonstrates
// parsing a plain C string into typed data entirely inside the plugin
// (no STL types cross the ABI - std::stod etc. only run inside this
// translation unit, never touch plugin_instance/plugin_metadata).

#include "plugin_abi.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct plugin_instance {
    int dummy;
};

extern "C" {

static plugin_status math_ops_init(plugin_instance **out_instance, const plugin_host_context *host_ctx) {
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

static plugin_status math_ops_execute(plugin_instance *instance, const char *input, char *output_buffer,
                                       size_t output_buffer_size, size_t *out_written) {
    if (instance == nullptr || input == nullptr || output_buffer == nullptr || output_buffer_size == 0) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }

    char op[16] = {0};
    double a = 0.0;
    double b = 0.0;
    int matched = sscanf(input, "%15[^,],%lf,%lf", op, &a, &b);
    if (matched != 3) {
        return PLUGIN_STATUS_INVALID_ARGUMENT;
    }

    double result = 0.0;
    if (strcmp(op, "add") == 0) {
        result = a + b;
    } else if (strcmp(op, "sub") == 0) {
        result = a - b;
    } else if (strcmp(op, "mul") == 0) {
        result = a * b;
    } else if (strcmp(op, "div") == 0) {
        if (b == 0.0) {
            return PLUGIN_STATUS_INVALID_ARGUMENT;
        }
        result = a / b;
    } else {
        return PLUGIN_STATUS_UNSUPPORTED_OPERATION;
    }

    int needed = snprintf(output_buffer, output_buffer_size, "%.6f", result);
    if (needed < 0 || static_cast<size_t>(needed) >= output_buffer_size) {
        return PLUGIN_STATUS_BUFFER_TOO_SMALL;
    }
    if (out_written != nullptr) {
        *out_written = static_cast<size_t>(needed);
    }
    return PLUGIN_STATUS_OK;
}

static plugin_status math_ops_shutdown(plugin_instance *instance) {
    delete instance;
    return PLUGIN_STATUS_OK;
}

static const plugin_metadata kMetadata = {
    PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(plugin_metadata)),
    "math_ops",
    "1.0.0",
    {math_ops_init, math_ops_execute, math_ops_shutdown},
};

const plugin_metadata *plugin_get_metadata(void) { return &kMetadata; }

}  // extern "C"
