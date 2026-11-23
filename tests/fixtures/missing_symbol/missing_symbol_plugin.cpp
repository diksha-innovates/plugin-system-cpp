// TEST FIXTURE - not a real plugin.
//
// Deliberately does NOT export plugin_get_metadata(). Used to verify
// the host reports a clean "missing required symbol" error and
// continues (does not crash dlsym-ing a symbol that isn't there).

#include "plugin_abi.h"

extern "C" {

// Exports some unrelated symbol so the shared library is non-empty and
// still a valid, loadable dynamic library - just without the one symbol
// the host actually looks for.
int missing_symbol_fixture_marker(void) { return 42; }

}  // extern "C"
