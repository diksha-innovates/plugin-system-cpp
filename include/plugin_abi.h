/*
 * plugin_abi.h
 *
 * The stable C ABI boundary between the host application and plugins.
 *
 * RULES ENFORCED BY THIS HEADER (read before touching it):
 *   1. Every type that crosses this boundary is POD / trivially copyable.
 *      No std::string, no std::vector, no std::function, no virtual
 *      functions, no exceptions. C++ vtable layout is not stable across
 *      compilers/compiler versions (and not even guaranteed stable across
 *      optimization settings of the *same* compiler), so no C++ class can
 *      ever be passed by value or by reference across this line.
 *   2. Opaque types (plugin_instance, plugin_host_context) are forward
 *      declared only. The host never sees their definition; only the
 *      plugin that allocated one knows its real layout. The host just
 *      carries the pointer around and passes it back to the plugin's own
 *      functions.
 *   3. plugin_metadata carries an explicit abi_version and struct_size so
 *      the host can detect, BEFORE calling a single function pointer,
 *      whether a plugin was built against an incompatible ABI version or
 *      a different (larger/smaller/differently-packed) struct layout.
 *   4. No exceptions may cross this boundary in either direction. All
 *      functions report failure via plugin_status return codes.
 *
 * This header is included by both the C++ host and every plugin (which
 * may themselves be implemented in C++, compiled as a separate shared
 * library with its own, possibly different, C++ runtime/STL ABI - which
 * is exactly why STL types must never appear here).
 */

#ifndef PLUGIN_SYSTEM_CPP_PLUGIN_ABI_H
#define PLUGIN_SYSTEM_CPP_PLUGIN_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump this whenever a breaking change is made to any struct defined in
 * this header (field added/removed/reordered, function pointer signature
 * changed, etc). A plugin built against a different ABI version must be
 * rejected by the host before any function pointer in its vtable is
 * called. */
#define PLUGIN_ABI_VERSION 1u

/* ------------------------------------------------------------------- */
/* Status codes                                                        */
/* ------------------------------------------------------------------- */

typedef enum plugin_status {
    PLUGIN_STATUS_OK = 0,
    PLUGIN_STATUS_ERROR = 1,
    PLUGIN_STATUS_INVALID_ARGUMENT = 2,
    PLUGIN_STATUS_NOT_INITIALIZED = 3,
    PLUGIN_STATUS_ALREADY_INITIALIZED = 4,
    PLUGIN_STATUS_BUFFER_TOO_SMALL = 5,
    PLUGIN_STATUS_UNSUPPORTED_OPERATION = 6
} plugin_status;

/* ------------------------------------------------------------------- */
/* Opaque handles                                                      */
/* ------------------------------------------------------------------- */

/* Forward-declared only. Defined entirely inside the plugin's own
 * translation unit; the host never dereferences it, only stores and
 * passes the pointer back. */
typedef struct plugin_instance plugin_instance;

/* Reserved for future host services handed to plugins (logging,
 * allocation callbacks, ...). Not used by the example plugins today but
 * kept in the init() signature so it can be extended without an ABI
 * break (a new pointer field could be added to a struct this points to,
 * since adding fields to something reached only through an opaque
 * pointer does not change any existing struct layout plugins rely on).
 */
typedef struct plugin_host_context plugin_host_context;

/* ------------------------------------------------------------------- */
/* Function pointer table (POD - no std::function)                    */
/* ------------------------------------------------------------------- */

/* Allocate and initialize plugin-private state. Called by the host
 * EXACTLY ONCE, immediately after a successful load + ABI check, before
 * any other vtable function is called. On success must write a non-null
 * instance pointer to *out_instance and return PLUGIN_STATUS_OK. On
 * failure must leave *out_instance untouched/null and return a non-OK
 * status; the plugin must not leak any resources it allocated before
 * failing. Must never throw past this boundary. */
typedef plugin_status (*plugin_init_fn)(plugin_instance **out_instance,
                                         const plugin_host_context *host_ctx);

/* Perform the plugin's work. input is a NUL-terminated C string owned by
 * the caller (read-only to the plugin). The plugin writes a
 * NUL-terminated result into output_buffer (capacity
 * output_buffer_size) and, if out_written is non-null, the number of
 * bytes written excluding the NUL terminator. Returns
 * PLUGIN_STATUS_BUFFER_TOO_SMALL if output_buffer_size is insufficient;
 * the plugin must not write past output_buffer_size bytes in that case
 * either. */
typedef plugin_status (*plugin_execute_fn)(plugin_instance *instance,
                                            const char *input,
                                            char *output_buffer,
                                            size_t output_buffer_size,
                                            size_t *out_written);

/* Release all plugin-private state. Called by the host EXACTLY ONCE,
 * only for an instance that was successfully produced by init(), and
 * always before the host unloads (dlclose/FreeLibrary) the shared
 * library. Must never throw past this boundary. After this call returns
 * the instance pointer is dangling and must never be used again by
 * either side. */
typedef plugin_status (*plugin_shutdown_fn)(plugin_instance *instance);

typedef struct plugin_vtable {
    plugin_init_fn init;
    plugin_execute_fn execute;
    plugin_shutdown_fn shutdown;
} plugin_vtable;

/* ------------------------------------------------------------------- */
/* Metadata                                                             */
/* ------------------------------------------------------------------- */

#define PLUGIN_NAME_MAX_LEN 64u
#define PLUGIN_VERSION_MAX_LEN 32u

/*
 * Returned by the well-known plugin_get_metadata() export. The host must
 * check abi_version and struct_size BEFORE reading (or trusting the
 * layout of) any other field, and before calling any vtable function.
 *
 * Field layout, and why each field exists:
 *   abi_version  - must equal PLUGIN_ABI_VERSION the host was compiled
 *                  with. Checked first, before struct_size, so a plugin
 *                  built for a totally different ABI generation (which
 *                  might not even have a struct_size field at this
 *                  offset) is rejected as early as possible.
 *   struct_size  - sizeof(plugin_metadata) as computed inside the
 *                  plugin's own translation unit. Guards against a
 *                  layout mismatch that keeps the same abi_version
 *                  number by mistake (e.g. a padding/alignment
 *                  difference from a mixed-ABI build), which abi_version
 *                  alone cannot catch.
 *   name         - fixed-size, NUL-terminated plugin name for logging.
 *   version      - fixed-size, NUL-terminated plugin version string
 *                  (plugin's own semantic version, independent of the
 *                  ABI version).
 *   vtable       - POD function pointer table, see above.
 *
 * Fixed-size char arrays (rather than char*) are used for name/version
 * so the struct is fully self-contained POD with no ownership/lifetime
 * questions and no allocator mismatch between host and plugin.
 */
typedef struct plugin_metadata {
    uint32_t abi_version;
    uint32_t struct_size;
    char name[PLUGIN_NAME_MAX_LEN];
    char version[PLUGIN_VERSION_MAX_LEN];
    plugin_vtable vtable;
} plugin_metadata;

/* Every plugin must export exactly one symbol with this name and
 * signature. The host resolves it with dlsym()/GetProcAddress() by name,
 * so this symbol must have C linkage (hence extern "C" for C++
 * plugins) - C++ name mangling would make the symbol name
 * compiler/version-dependent and impossible to look up reliably. */
typedef const plugin_metadata *(*plugin_get_metadata_fn)(void);

#define PLUGIN_GET_METADATA_SYMBOL "plugin_get_metadata"

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_SYSTEM_CPP_PLUGIN_ABI_H */
