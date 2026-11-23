/*
 * plugin_helper.h
 *
 * Small header-only convenience shared by the example/fixture plugins in
 * THIS project only (it is not part of the ABI - plugins are free to not
 * use it at all, as long as they export plugin_get_metadata() correctly).
 * It just fills in the fixed-size name/version fields safely.
 */
#ifndef PLUGIN_SYSTEM_CPP_PLUGIN_HELPER_H
#define PLUGIN_SYSTEM_CPP_PLUGIN_HELPER_H

#include "plugin_abi.h"

#include <string.h>

static inline void plugin_helper_copy_fixed(char *dst, size_t dst_len, const char *src) {
    size_t n = strlen(src);
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

#endif /* PLUGIN_SYSTEM_CPP_PLUGIN_HELPER_H */
