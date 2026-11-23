# Interview Questions — plugin-system-cpp

All answers are written in my own voice, grounded in this codebase. File/function references point at real locations as of this build.

## A. Pitches and architecture walkthrough

**1. Give me the 60-second pitch.**
This is a C++ host that loads plugins at runtime from shared libraries (`.dylib`/`.so`, and an untested `.dll` path), across a versioned, POD-only C ABI (`include/plugin_abi.h`). The whole point is that the host and a plugin can be built by different compilers, or different versions of the same compiler, and still work together safely — which is only true if nothing non-POD (no `std::string`, no virtual functions, no STL containers) ever crosses the boundary. The host (`PluginManager`, `src/host/plugin_manager.cpp`) checks an explicit `abi_version` and `struct_size` before it trusts anything else in a plugin's metadata, calls `init()` exactly once, `execute()` any number of times, and `shutdown()` exactly once (idempotent, guarded), and unloads plugins in strict reverse order of load. 21 GoogleTest cases cover every documented failure mode, and all pass under ASan+UBSan and ThreadSanitizer.

**2. Give me the 3-minute version.**
Start from the ABI header, because everything else follows from it: `plugin_metadata` has `abi_version` and `struct_size` as its first two fields specifically so the host can validate them before trusting the rest of the struct, including the function-pointer vtable (`init`/`execute`/`shutdown`). `plugin_instance` is forward-declared and opaque — the host never looks inside it, it just holds the pointer and hands it back to the plugin's own functions. The one exported symbol every plugin must have, `plugin_get_metadata`, has C linkage so name mangling can't make it unfindable.
On the host side, `DynamicLibrary` (`src/host/dynamic_library.cpp`) is a move-only RAII wrapper around `dlopen`/`dlsym`/`dlclose` — `RTLD_NOW` deliberately, so a plugin's own internal missing-symbol problems surface at load time instead of nondeterministically later. `PluginManager::load()` is the single choke point for every failure mode the spec calls out: file-not-found, open-failed, missing-symbol, null-metadata, ABI-version-mismatch, struct-size-mismatch, missing-vtable-function, init-failed — each becomes a distinct `LoadError` with a message, and the `DynamicLibrary` member unwinds via RAII on every early return, so nothing is ever left half-loaded.
`LoadedPlugin::shutdown_once()` is the idempotency guard — a `PluginLifecycleState` enum ensures the plugin's real `shutdown()` runs at most once no matter how many times host code calls it, and once it has run, `instance_` is set to `nullptr` so a subsequent `execute()` fails cleanly (`PLUGIN_STATUS_NOT_INITIALIZED`) instead of dereferencing a dangling pointer. `PluginManager::unload_all()` pops a stack back-to-front, which is strict reverse-of-load order by construction. Three example plugins and six broken/instrumented fixtures exercise all of this; the `order_tracker` fixture even logs its own `init`/`shutdown` calls from *inside* the plugin, so unload order is verified from the plugin's perspective, not just trusted from host bookkeeping.

**3. Walk me through the architecture, module by module, as if I've never seen this repo.**
Four layers, outside-in: (1) `include/plugin_abi.h` — the contract, pure C, POD only. (2) `src/host/dynamic_library.{hpp,cpp}` — the OS-primitive wrapper, one layer above raw `dlopen`. (3) `src/host/plugin_manager.{hpp,cpp}` — the policy layer: lifecycle rules, failure taxonomy, ordering guarantees. (4) `src/host/main.cpp` — a thin CLI over layer 3. Plugins (`plugins/*/`) and fixtures (`tests/fixtures/*/`) are separate shared libraries compiled against layer 1 only; they never see layers 2–4. Tests (`tests/unit/*.cpp`) link layer 2+3 directly and load the plugins/fixtures as data, the same way the real host would.

## B. Module deep-dives

**4. Why does `plugin_metadata` put `abi_version` and `struct_size` as the *first two* fields?**
So the host can read and validate them via fixed, ABI-version-independent offsets even against a hypothetical future ABI generation whose struct might not share any other field with today's. If `abi_version` were field 5, a sufficiently different future struct might not have a field 5 at all, or might have something else there — checking it first, at a fixed early offset, is the only way to make the check itself version-independent.

**5. What breaks if I add a `bool enabled` field to the middle of `plugin_metadata`?**
Every plugin's statically-initialized `kMetadata` struct still compiles (it's POD, new field gets implicitly zero/default), but `sizeof(plugin_metadata)` changes, which is exactly what `struct_size` exists to catch — old plugins built against the previous layout would now fail `StructSizeMismatch` at load time instead of being silently misread. That's the correct, intended failure mode for a struct-layout change; the real fix would be bumping `PLUGIN_ABI_VERSION` too, documenting the break, and rebuilding plugins against the new header.

**6. Why `RTLD_NOW | RTLD_LOCAL` instead of `RTLD_LAZY`?**
`RTLD_LAZY` would resolve each symbol the first time it's actually called, meaning a plugin binary with an internal missing-symbol bug (e.g. it references a libc++ symbol not available in its runtime) could load "successfully" and only crash much later, the first time some rarely-exercised code path runs. `RTLD_NOW` pays a small, one-time, per-load cost to resolve everything immediately, so that failure surfaces in `DynamicLibrary::load()`, the one place already set up to handle it. `RTLD_LOCAL` (instead of `RTLD_GLOBAL`) keeps a plugin's symbols from leaking into the global symbol table and colliding with another plugin's same-named internal symbols.

**7. Why does `resolve_symbol()` call `dlerror()` twice — once before, once after `dlsym()`?**
Because `dlsym()` returning `nullptr` is ambiguous: it's the correct return value both when the symbol doesn't exist *and* when the symbol exists but its real address happens to be null. The documented way to disambiguate is to clear `dlerror()` immediately before the call, then check `dlerror()` again after — if it's non-null now, the lookup genuinely failed; if it's still null, the symbol was found and its value really is null (or non-null, whatever was returned).

**8. What happens if two loaded plugins both have an internal (non-exported, `static`) symbol with the same name?**
Nothing — that's exactly what `RTLD_LOCAL` is for. Each plugin's symbols are local to its own handle and don't get added to the global process-wide symbol table, so two plugins can both have a `static` function called `helper()` with zero collision. Only the one contractually-required exported symbol, `plugin_get_metadata`, has to be unique in the sense that it's the only thing the host ever looks up by name per plugin.

**9. Walk me through what happens, step by step, when `PluginManager::load()` is called on a file that doesn't exist.**
`std::filesystem::exists(path, ec)` returns false (or sets `ec`), so `load()` returns immediately with `LoadError::FileNotFound` and a message containing the path. `DynamicLibrary` is never even constructed with an open handle — no `dlopen` call happens at all. This is deliberately checked before attempting `dlopen` so the error message can say "file not found" specifically, rather than whatever less-specific error the OS loader would give for a nonexistent path.

**10. Why is the `DynamicLibrary` passed into `LoadedPlugin`'s constructor by value (moved), not kept in `PluginManager` separately?**
Ownership should live with the thing it protects. `LoadedPlugin` owns exactly one library handle and one plugin instance together — they have the same lifetime (the instance is meaningless once its library is unloaded) — so bundling them in one RAII object means there's only one place that can get the destruction order wrong, and `LoadedPlugin`'s destructor-implicit member order (library declared after metadata/instance... actually see Q11) handles it correctly by construction.

**11. `LoadedPlugin` stores `DynamicLibrary library_` as its first data member. Why does member order matter here?**
C++ destroys members in reverse declaration order. `library_` is declared first, so it's destroyed *last* — after `metadata_`, `instance_`, `name_`, `version_`, `state_` have all been torn down. That's actually fine either way here since none of those other members have destructors that touch the library, but the more important invariant is in `shutdown_once()`: it explicitly calls the plugin's `shutdown()` *before* the `LoadedPlugin` is destroyed and the library unloaded (`PluginManager::unload_all()` calls `shutdown_once()` then `pop_back()`), never relying on destructor order alone to get "shutdown before dlclose" right.

**12. What does `shutdown_once()` guarantee, and how does it guarantee it?**
That the plugin's real `shutdown()` function pointer is invoked at most once, ever, for a given `LoadedPlugin`, no matter how many times `shutdown_once()` itself is called. The guard is a `PluginLifecycleState state_` enum: the first call transitions `Initialized → ShutDown`, calls the plugin's `shutdown()`, and nulls `instance_`; every subsequent call sees `state_ == ShutDown` and returns `PLUGIN_STATUS_OK` as a no-op without touching `instance_` or calling anything in the plugin again.

**13. Why null out `instance_` inside `shutdown_once()` instead of just setting a "shut down" boolean?**
Belt-and-suspenders against a future bug: even if something bypassed the `state_` check (it shouldn't be possible given the current code, but defensive coding against *my own* future changes), a nulled `instance_` passed to `execute()` is something that can be checked, vs. a dangling non-null pointer that would need the checker to somehow know it's invalid. `execute()` actually checks `state_`, not `instance_ == nullptr`, as the primary guard — nulling the pointer is the secondary, harder-to-misuse-by-accident layer.

**14. What does `load_directory()` do with a directory containing one subdirectory and one plugin file?**
It calls `entry.is_regular_file(ec)` on every directory entry and skips anything that isn't a regular file — so a subdirectory is silently skipped, not recursed into, and not reported as a failure. This is a non-recursive, single-level scan by design (see Q "why not recursive directory scan" below).

**15. Why sort `candidates` before loading them in `load_directory()`?**
`std::filesystem::directory_iterator` does not guarantee any particular traversal order (it reflects whatever order the underlying OS directory entries come back in, which can vary by filesystem and isn't guaranteed stable even between two runs). Sorting makes the scan order — and therefore the resulting `LoadResult` vector order and the load-stack order — deterministic and reproducible, which matters for both the tests (`test_directory_scan.cpp`'s mixed-directory test asserts specific result content) and for debugging a real scan later.

**16. Why does `main.cpp`'s CLI take a single fixed-size `char buffer[1024]` for `execute()` output instead of something growable?**
Because that's the real ABI contract being exercised: `execute()` takes a caller-owned buffer and a size, and returns `PLUGIN_STATUS_BUFFER_TOO_SMALL` if it doesn't fit — there is no way to hand back a growable buffer across this ABI (that would require the plugin to allocate with the host's allocator, which the ABI explicitly avoids). 1024 bytes is just a CLI-demo-sized default; a real embedding host would size the buffer to its own needs or call `execute()` in a loop with retry-on-`BUFFER_TOO_SMALL` and a bigger buffer.

**17. What's the difference between `math_ops`'s `plugin_instance` and `logging_counter`'s?**
`math_ops`'s instance is a placeholder (`struct plugin_instance { int dummy; }`) — the plugin has no real per-instance state, since every `execute()` call is self-contained (parse `"op,a,b"`, compute, write result). `logging_counter`'s instance holds a real call counter incremented on every `execute()`, demonstrating actual plugin-private state that persists across calls and is owned entirely inside the plugin's own translation unit — the host never sees its layout, only the opaque pointer.

**18. The `order_tracker` fixture is built three times from one source file. How, and why not just write three files?**
`CMakeLists.txt` defines three `MODULE` targets (`fixture_order_tracker_a/b/c`) all compiling the same `order_tracker_plugin.cpp`, each with a different `-DORDER_TRACKER_NAME="order_tracker_X"` compile definition that the source reads to decide its own name and log-line prefix. One source file, no duplication, three distinct shared libraries — exactly what you'd want for "three different plugins that behave identically except for an identifying name," which is all this fixture needs to prove load/unload order.

**19. Why does `order_tracker` write to a file via an environment variable instead of exposing a `fixture_get_log()` symbol like `shutdown_counter` does?**
Because the thing being tested is *process-wide* state across three separate shared-library instances (A, B, C), not per-instance state inside one library. A file is a trivially shared, host-independent channel all three can append to; an exported-symbol approach would need some other shared location anyway (a `static` in each plugin is local to that plugin alone and wouldn't let A's log and C's log be compared for ordering in one place).

## C. Design trade-offs — "why not X?"

**20. Why not use C++ abstract base classes across the ABI instead of a C vtable struct?**
Covered in the README's "Design decisions" section at length, but the short version: C++ vtable layout (and RTTI, and exception-table layout) is not guaranteed stable even across two builds of the *same* compiler with different optimization flags, let alone across different compiler vendors or standard library implementations. A POD struct of C function pointers has an ABI fixed by the platform's C calling convention, which actually is stable across all of that. The entire premise of a plugin system — host and plugin built independently — requires the stable option.

**21. Why not let plugins allocate `plugin_instance` with `malloc`/`new` from the host's allocator, passed in via `host_ctx`?**
I didn't build a host-allocator-callback mechanism at all in this version — `host_ctx` is reserved (currently always passed as `nullptr`) for exactly this kind of future extension. Each plugin currently uses its own `new`/`delete` (or could use `malloc`/`free`) inside its own translation unit, and the host never allocates or frees anything on the plugin's behalf. That's simpler and avoids a cross-ABI allocator-mismatch bug class entirely, at the cost of not being able to track/limit a plugin's memory usage from the host side — an acceptable trade for a trusted-plugin model.

**22. Why not make `load_directory()` recursive?**
A plugin directory is meant to be a flat, curated set of shared libraries a host operator has deliberately placed there. Recursing would mean guessing at a policy for how deep to go, what to do with symlinked subdirectories (loop risk), and whether a stray build artifact two levels down should silently become a loaded plugin. Keeping it single-level, non-recursive is simpler and matches how the demo (`good_plugins`/`mixed_plugins`, assembled flat by CMake) is actually organized.

**23. Why report every file in `load_directory()`'s result vector, including failures, instead of just the successes?**
Because a host operator scanning a plugin directory needs to know *which* file failed and *why*, not just how many succeeded — that's exactly the UX the CLI's `scan` mode and `test_directory_scan.cpp`'s mixed-directory test both rely on. Silently dropping failures would make a misconfigured or corrupted plugin directory much harder to diagnose.

**24. Why is `PluginManager` non-copyable?**
It owns a stack of `std::unique_ptr<LoadedPlugin>`, each of which owns exactly one real OS resource (a `dlopen` handle) and one real plugin-side resource (an initialized `plugin_instance`). Copying a `PluginManager` would mean either a shallow copy (two managers both thinking they own, and will `dlclose`, the same handle — a double-unload bug) or a deep "copy" that would require re-`dlopen`-ing and re-`init()`-ing every plugin, which isn't actually a copy of anything meaningful. Deleting the copy operations and leaving it move-only-by-omission (moves aren't declared either, so they're implicitly deleted too since there's a user-declared destructor) makes the only valid ownership model the only *expressible* one.

**25. Why `std::unique_ptr<LoadedPlugin>` in the vector instead of storing `LoadedPlugin` by value?**
`LoadedPlugin` is already move-only (it holds a move-only `DynamicLibrary`), so `std::vector<LoadedPlugin>` would work mechanically. I used `unique_ptr` anyway so that `plugin_at()` can return a stable reference (`LoadedPlugin&`) that survives the vector reallocating when a new plugin is pushed — a `vector<LoadedPlugin>` reallocation would move-construct every existing element to new storage, invalidating any reference a caller was holding; a `vector<unique_ptr<LoadedPlugin>>` reallocation only moves the pointers, leaving the pointed-to objects (and any reference into them) stable.

**26. Why does `shutdown_once()` return `PLUGIN_STATUS_OK` on a repeated call instead of a distinct "already shut down" status?**
Because the contract is "safe to call as many times as you want, and it's a no-op after the first" — from the caller's point of view, "the plugin is shut down, as requested" is success, not an error. Introducing a distinct status would force every caller (including `unload_all()`'s internal loop) to special-case it, for no behavioral benefit; the existing `PluginLifecycleState::state()` accessor is there if a caller actually needs to distinguish "just shut it down" from "it was already shut down."

## D. C++ language questions the code touches

**27. Where does RAII actually show up in this codebase, concretely?**
`DynamicLibrary`'s destructor calls `unload()` (→ `dlclose`), so a `DynamicLibrary` going out of scope — including via an early `return` inside `PluginManager::load()` after a partial failure — always unloads the library exactly once, with no explicit cleanup code needed at each failure site. `LoadedPlugin` composes a `DynamicLibrary` by value for the same reason one level up.

**28. Why is `DynamicLibrary` move-only, and how are the move operations implemented?**
Copying a library handle would mean two objects both believing they own, and will `dlclose`, the same OS handle — a double-close bug. The move constructor (`dynamic_library.cpp:21`) steals `other.handle_`/`path_`/`last_error_` and sets `other.handle_ = nullptr`, leaving `other` in a valid-but-unloaded state so its destructor's `unload()` call is a safe no-op. Move assignment (`:26`) additionally calls `unload()` on `*this` first, to release whatever `*this` currently owns before taking over `other`'s handle — otherwise the overwritten handle would leak.

**29. What's the self-move-assignment guard in `DynamicLibrary::operator=(DynamicLibrary&&)` protecting against?**
`if (this != &other)` guards against `a = std::move(a)` — without it, `unload()` would run on `*this`/`other` (the same object), nulling `handle_`, and then the subsequent lines would copy the now-nulled `handle_` back into itself, which happens to be harmless here (nulling-then-reading-null is fine) but is still the standard defensive pattern for move-assignment, and matters more in classes where resources are acquired as part of assignment rather than just moved.

**30. Why does the code use `std::unique_ptr` and never a raw owning pointer?**
Every heap allocation that the host owns has exactly one clear owner and a well-defined lifetime tied to a scope or a container — `std::make_unique<LoadedPlugin>` inside `PluginManager::load()`, stored in `std::vector<std::unique_ptr<LoadedPlugin>> plugins_`. A raw owning pointer would require manual `delete` at every exit path (including exceptions, though none are thrown here deliberately) and offers no compiler-enforced single-ownership guarantee; `unique_ptr` makes "who deletes this, and when" unambiguous from the type alone.

**31. There are no templates in this codebase. Was that a deliberate choice?**
Yes, in the sense that nothing here needed one — the problem is "load a bounded set of distinct concrete types (`DynamicLibrary`, `LoadedPlugin`, `PluginManager`) with one clear responsibility each," not a generic-algorithm or generic-container problem. Introducing a template (e.g. a generic `Resource<T, DeleterFn>` RAII wrapper) to replace the one-off `DynamicLibrary` would add a layer of indirection for a abstraction used exactly once — premature generalization for no present benefit.

**32. What's the single most important place undefined behavior is being actively prevented in this code, and how?**
Calling a function pointer, or dereferencing a pointer, into a shared library's address space *after* that library has been `dlclose`'d. This is real UB (the memory may be unmapped, or remapped to something else entirely) and it's exactly what `shutdown_once()`'s `instance_ = nullptr` plus `execute()`'s `state_` check is designed to prevent on the *host* side. I also hit the same class of bug in a *test* (see the debugging story below) by calling a resolved extra-symbol function pointer after `unload_all()` had already `dlclose`'d its library — proof that the discipline has to be maintained by every caller, not just inside the library that owns the handle.

**33. Does this project use `std::atomic` or touch the C++ memory model at all?**
No — the host/plugin interaction is deliberately single-threaded (see README "Limitations"), so there's no shared mutable state being accessed from more than one thread and no need for atomics, memory ordering, or fences anywhere in this codebase. `ENABLE_TSAN` exists and is run in CI purely for consistency with the ASan/UBSan option pattern used across my other projects; it passes here trivially because there is no concurrency to race on, not because of any clever lock-free design.

**34. Where does struct alignment/padding matter in this code, and how is it handled?**
`plugin_metadata`'s `struct_size` check exists specifically because alignment/padding can differ between two builds that are otherwise "compatible" in every other visible way (e.g. different struct-packing pragmas, or a 32-bit vs 64-bit build putting different padding after `uint32_t abi_version` before the next field) — `sizeof(plugin_metadata)` captures the net effect of all of that in one number the host can compare without needing to know *why* two layouts differ, only *whether* they do.

**35. Why `extern "C"` around every plugin's exported functions, including `plugin_get_metadata`?**
C++ name mangling encodes argument types (and sometimes more) into the symbol name, and that encoding is not standardized across compilers/versions — so a C++ function's *mangled* name is exactly the kind of thing `dlsym`-by-string-name can't reliably look up across a host/plugin compiled by different toolchains. `extern "C"` forces plain C linkage, giving a fixed, predictable symbol name (`plugin_get_metadata`) regardless of what compiler built the plugin.

**36. Why no exceptions anywhere in this codebase, host or plugin side?**
Plugin side: an exception thrown inside a plugin and allowed to propagate across the `dlopen` boundary into the host is undefined behavior — C++ exception unwinding relies on compiler-specific metadata (unwind tables, personality routines) that is not guaranteed compatible between two independently-compiled binaries, especially across different compilers. So the ABI rule is absolute: no exceptions cross it, ever, in either direction — hence every ABI function returns a `plugin_status` instead. Host side (`DynamicLibrary`, `PluginManager`): I kept the same discipline even though it's technically not required there (host-internal code never crosses the ABI boundary by definition), purely for *consistency* — one error-handling style throughout the whole loader, not two, so nobody reading this code has to remember "exceptions are fine here but not there."

**37. What's the Big-O of `PluginManager::plugin_at(index)`?**
O(1) — it's `plugins_.at(index)`, a bounds-checked `std::vector` index, which throws `std::out_of_range` on an invalid index (the one place in host-internal code where an exception *is* allowed, since it never crosses the plugin ABI — it's a programmer-error signal to whoever called `plugin_at` with a bad index, analogous to how the STL itself uses exceptions for `.at()`).

## E. OS/systems concepts touched

**38. What actually happens, at the OS level, when `DynamicLibrary::load()` calls `dlopen()`?**
The dynamic linker (`dyld` on macOS, `ld.so` on Linux) maps the shared library's segments into the calling process's virtual address space, resolves (with `RTLD_NOW`) every undefined symbol the library references against already-loaded libraries and the library itself, runs any static initializers in the library (including C++ global/static object constructors, which is how each plugin's `static const plugin_metadata kMetadata` ends up initialized before `plugin_get_metadata()` is ever called), and returns an opaque handle identifying the newly-mapped library within this process.

**39. What happens at the OS level on `dlclose()`, and when is it actually unmapped?**
`dlclose()` decrements the library's reference count within the process; only when it reaches zero does the dynamic linker run static destructors and actually unmap the library's segments from the process's virtual address space. In this codebase each plugin is only ever opened once (one `DynamicLibrary` per load), so the reference count is always exactly 1→0, meaning `dlclose()` here does trigger an immediate real unmap — which is precisely why calling a function pointer resolved from that library afterward is genuine, not theoretical, undefined behavior.

**40. Why does the directory-scan / plugin-loading logic never need to worry about file descriptor limits?**
`dlopen`/`dlclose` manage their own internal handles to the mapped library (via `mmap`, which does briefly use an fd during the mapping setup, then can close it), not a file descriptor this code holds open and must track — there's no `open()`/`close()` pair in this codebase's control flow for plugin files at all. Contrast with, e.g., a project reading a file's bytes directly, which would need explicit fd bookkeeping.

**41. How does virtual memory make `RTLD_LOCAL` possible?**
Each process has its own virtual address space, and within one process the dynamic linker maintains a private, per-library symbol-resolution scope for anything loaded `RTLD_LOCAL` — so two plugins loaded into the same process can each have an internal symbol at the same *name* without collision, because the linker's local symbol table for library A is consulted only when resolving A's own undefined references, never when resolving B's.

**42. Why is `std::filesystem::exists()` checked before `dlopen()` in `PluginManager::load()`, given that `dlopen()` would also fail on a nonexistent file?**
Purely for error-message quality: `dlopen()` failing on a missing file gives a generic "image not found" style error from the dynamic linker, whereas checking existence first lets the host report a specific `LoadError::FileNotFound` with a clean message before even attempting to open anything. It's a small redundant check (one extra `stat`-like syscall) in exchange for a much clearer failure mode in the common case of a typo'd path.

**43. This host is a single process loading multiple shared libraries. How is that different from spawning multiple child processes, one per plugin?**
In-process loading (what this project does) is cheap and fast (no `fork`/`exec`, no IPC) but gives a misbehaving or malicious plugin full access to the host's address space and privileges — a crash in a plugin crashes the whole host. A multi-process design (mentioned in README "Future work") would trade that speed and simplicity for real isolation: a plugin crash would only kill its own process, and IPC (pipes, shared memory, a serialization protocol) would replace the direct function-pointer vtable. This project deliberately chose the simpler, faster, trusted-plugin model and documents the isolation trade-off rather than hiding it.

**44. Why does the Windows path use `LoadLibraryA` instead of `LoadLibraryW`?**
Simplicity, and because this codebase already passes `std::string` (narrow/ASCII-ish paths) everywhere rather than wide strings — `LoadLibraryA` takes the same narrow-char path type with no conversion needed. The trade-off (documented as a limitation) is that `LoadLibraryA` paths are interpreted using the system's current ANSI code page, which can mishandle non-ASCII paths; a production Windows build would likely want `LoadLibraryW` with explicit UTF-8→UTF-16 conversion. Since this path is untested anyway, I kept it as simple as possible rather than over-engineering something I can't verify.

## F. Concurrency, correctness, and edge cases

**45. Is any part of this codebase thread-safe?**
No shared mutable state is ever accessed from more than one thread, because the entire design is single-threaded by contract (host calls into `PluginManager` sequentially; a plugin's `execute()` is only ever called by the host, never concurrently with itself for the same instance). `ENABLE_TSAN` runs clean in CI, but that's a statement about absence of races given single-threaded usage, not a claim of thread-safety under concurrent use — calling `manager.plugin_at(0).execute(...)` from two threads simultaneously on the *same* `LoadedPlugin` is unsupported and would be a data race on the plugin's own internal state (e.g. `logging_counter`'s call counter) if attempted.

**46. What happens if `execute()` is called on index 5 when only 3 plugins are loaded?**
`PluginManager::plugin_at(5)` calls `plugins_.at(5)`, which throws `std::bounds_error`/`std::out_of_range` — the one place in this codebase an exception is allowed, because it's host-internal (caller-error) territory, never crossing into plugin code. The CLI (`main.cpp`) doesn't call `plugin_at` with an out-of-range index in its own control flow (it always uses the index just returned by `load()`), so this path is really a safety net for host-internal misuse, not something routinely exercised by the shipped CLI.

**47. Edge case: a plugin's `plugin_get_metadata()` returns a valid, correctly-sized struct, but `vtable.execute` is a null function pointer. What happens?**
`load()` only requires `vtable.init` and `vtable.shutdown` to be non-null (`LoadError::MissingVtableFunction` if either is missing); `vtable.execute` being null is allowed at load time (not every plugin necessarily needs to support `execute`). `LoadedPlugin::execute()` checks `metadata_->vtable.execute == nullptr` itself and returns `PLUGIN_STATUS_UNSUPPORTED_OPERATION` rather than calling through a null pointer.

**48. Edge case: what if `init()` writes a non-null `*out_instance` but then returns a non-OK status?**
The host's contract (documented in `plugin_abi.h`'s comment on `plugin_init_fn`) is that the plugin must leave `*out_instance` untouched/null on failure — the host trusts that contract and does not attempt to call `shutdown()` on whatever `init()` may have (incorrectly) written in this case, because without a status of OK, the host doesn't know if that pointer is safe to pass to `shutdown()`. This is a documented plugin-side obligation, not something the host can independently verify, which is exactly why `init_fail_plugin.cpp`'s own code comment calls out "Contractually: on failure we must NOT write to `*out_instance`."

**49. Edge case: calling `shutdown_once()` on a `LoadedPlugin` whose `init()` never actually ran (hypothetically).**
Can't happen through the public API — `LoadedPlugin` objects are only ever constructed by `PluginManager::load()` after `init()` has already returned `PLUGIN_STATUS_OK` with a non-null instance (see the `load()` call sequence); there's no code path that produces a `LoadedPlugin` without a successful prior `init()`. This is enforced by `load()`'s control flow, not by a runtime check inside `LoadedPlugin` itself.

**50. What guarantees that a `LoadResult` with `success == false` never has a stale/garbage `index`?**
`LoadResult::index` defaults to `0` and is only ever assigned (`result.index = plugins_.size() - 1`) on the success path, right before `result.success = true` is set — every early-`return` failure path returns before that assignment is reached, so a failed `LoadResult`'s `index` is always its default `0`, which callers are expected to ignore when `success` is false (the field's doc comment says "valid only if success").

## G. Testing, sanitizers, CI, and benchmarking

**51. Why GoogleTest instead of a hand-rolled test runner or Catch2?**
GoogleTest has first-class CMake `FetchContent` support, `gtest_discover_tests()` integration with CTest (so each `TEST()` shows up as an individually runnable/filterable CTest case, which matters for the clean `ctest --output-on-failure` output this project relies on), and it's the same tool I used across my other C++ projects in this set — not shared code (each project's CMakeLists fetches its own copy independently), just a consistent choice.

**52. Why `GIT_SHALLOW TRUE` on the GoogleTest `FetchContent_Declare`?**
Purely a build-environment practicality: on a slow/constrained network, a full (non-shallow) clone of GoogleTest's entire git history is dramatically slower than a shallow, single-tag clone — I measured a plain `git clone --depth 1 --branch v1.15.2` taking about 90 seconds on this machine's network versus a configure step that hadn't finished after 10 minutes without `GIT_SHALLOW`. It has no effect on reproducibility since the tag (`v1.15.2`) is still pinned exactly.

**53. Why does `ASAN_OPTIONS=detect_leaks=1 ctest ...` make every single test "fail" with "Subprocess aborted"?**
Because AddressSanitizer's LeakSanitizer component is not implemented on macOS at all — when you force `detect_leaks=1` anyway, ASan itself prints `AddressSanitizer: detect_leaks is not supported on this platform` and aborts the process immediately, before any test code even runs. This isn't a bug in this project; it's a known Apple-platform gap in ASan. The fix is simply not forcing `detect_leaks=1` on macOS (it's off by default there); leak checking genuinely runs under ASan on Linux, which is why the CI matrix includes `ubuntu-latest`.

**54. Walk me through why `ENABLE_ASAN` and `ENABLE_UBSAN` can't both be requested alongside `ENABLE_TSAN` in the same binary.**
`CMakeLists.txt` has `if(ENABLE_TSAN AND (ENABLE_ASAN OR ENABLE_UBSAN)) message(FATAL_ERROR ...)`. This mirrors a real Clang/GCC limitation: ThreadSanitizer's and AddressSanitizer's runtime instrumentation and shadow-memory schemes are mutually incompatible in one binary — you genuinely cannot link both into the same executable. That's why this project builds three *separate* directories (`build`, `build-asan`, `build-tsan`) rather than one build with every sanitizer flag on.

**55. What exactly does `gtest_discover_tests()` do that `add_test()` wouldn't?**
`add_test()` would register the whole `plugin_system_tests` binary as one single CTest "test" — pass/fail as a unit, with no way to see which of the 21 `TEST()` cases failed without reading the combined stdout. `gtest_discover_tests()` runs the binary once at *build* time with `--gtest_list_tests`, parses the names, and registers each one as its own CTest entry — which is exactly why `ctest --output-on-failure` can report `14/21 ... SEGFAULT` pinpointing the one failing case instead of just "the test binary failed."

**56. Why is there no formal benchmark suite for this project, unlike some of my others?**
Because this project's job is correctness of a loader (does it detect every documented failure mode, does it maintain lifecycle invariants), not throughput — `dlopen`/`dlsym`/`dlclose` costs are dominated by the OS dynamic linker, not by anything in this codebase's control, so a micro-benchmark here would mostly be measuring the OS, not my code. The README includes one honestly-labeled, informal, real-measured number (200 CLI invocations, wall-clock total) specifically called out as *not* a `dlopen` micro-benchmark, rather than inventing or implying a rigor this project doesn't have.

**57. How would you add a proper benchmark for just the `dlopen`/`dlsym`/`init` path, isolated from process-spawn overhead?**
Link a benchmark harness (e.g. Google Benchmark) directly against `plugin_host_lib`, call `PluginManager::load()`/`unload_all()` in a loop inside one long-lived process (removing fork/exec overhead entirely), and report per-iteration median/p99 — the same shape of benchmark I used for allocator/proxy throughput in my other projects, just not built here because it wasn't needed to validate this project's actual risk area (correctness of failure handling, not speed).

## H. Real debugging stories from this build

**58. Tell me about an actual bug you hit while building this, not a hypothetical.**
`test_lifecycle.cpp`'s `Lifecycle.ShutdownFiresExactlyOnceAcrossMultipleHostOperations` test segfaulted under plain `ctest` (not even under a sanitizer — a hard crash). The test loaded the `shutdown_counter` fixture, resolved its `fixture_get_shutdown_count` extra-symbol function pointer, called `shutdown_once()` twice to check the counter stays at 1, then called `manager.unload_all()` — which `dlclose()`s the library — and *then* called `get_count()` again to assert the counter still read 1. That last call was the bug: `get_count` is a function pointer into code that lived inside the just-`dlclose()`'d shared library, so by the time it was called, the address it pointed to was no longer mapped. Classic use-after-unload, and SIGSEGV is exactly the symptom you'd expect.

**59. How did you actually find and fix it, step by step?**
`ctest --output-on-failure` reported test #14 as `***Exception: SegFault`, which immediately told me it was a crash, not a logic assertion failure — different investigation path. I reread the test line-by-line looking for anything dereferencing memory whose lifetime had ended, and found `manager.unload_all();` immediately followed by `EXPECT_EQ(get_count(), 1);` — the ordering made the bug obvious once I was looking at lifetime rather than logic. The fix was restructuring the test: move the "does an extra defensive call stay a no-op" check to a *third* explicit `shutdown_once()` call made *before* `unload_all()` (while the library is still mapped), and drop the post-`unload_all()` counter read entirely, since there is no safe way to observe that counter once its owning library has been unloaded — I documented exactly that reasoning in a comment at the new call site so the same mistake doesn't get reintroduced later.

**60. Did that bug reveal anything about the host code itself, or was it purely a test bug?**
Purely a test bug — the host's own discipline (`shutdown_once()` nulling `instance_`, `execute()` checking lifecycle state before touching the instance) was never actually exercised incorrectly here; the host was never asked to do anything unsafe. What it *did* validate, by contrast, is that `ExecuteAfterShutdownFailsCleanlyInsteadOfCrashing` (a different test, which calls `execute()` — a host API — after shutdown, not a raw resolved function pointer after unload) correctly returns `PLUGIN_STATUS_NOT_INITIALIZED` instead of crashing. The bug was specifically about calling a function pointer that bypasses the host's own safety checks entirely (resolved directly via `resolve_extra_symbol`, used only by tests for diagnostics) after the backing library was gone — a hazard inherent to that diagnostic mechanism, not to the production `execute()` path.

**61. Was there anything surprising about getting the build environment itself working?**
Yes — `cmake`/`ctest` weren't on `PATH` at all in the environment I built this in, and Homebrew's own prefix (`/opt/homebrew`) wasn't writable without `sudo`, which I'm not allowed to use. I installed `cmake` via the official PyPI wheel (`pip install --user cmake`, which bundles prebuilt binaries, not a source build) and symlinked it into `/opt/homebrew/bin`, which *was* writable and already on `PATH`. I also hit a very slow network when CMake's `FetchContent` tried a full (non-shallow) clone of GoogleTest's git history — adding `GIT_SHALLOW TRUE` to the `FetchContent_Declare` call turned a clone that hadn't finished in 10 minutes into one that completes in about 90 seconds, since it only fetches the one tagged commit instead of the whole history.

## I. Scaling and production follow-ups

**62. How would you make this 10x faster?**
Honestly, the host-side code is already not the bottleneck — `dlopen`/`dlsym`/`dlclose` costs are OS-controlled and dominate any load/unload cycle. The one thing actually worth speeding up is the CLI demo path, which pays full process-spawn cost (fork+exec) per invocation; a long-lived host process that loads once and serves many `execute()` calls (which is the intended real-world usage pattern anyway — the CLI's "one-shot per process" shape is a demo convenience, not the design target) would remove that entirely. There's no hot loop inside `PluginManager`/`DynamicLibrary` itself to optimize.

**63. What would "production-ready" mean for this project, concretely?**
At minimum: the Windows path actually built and tested on Windows CI (currently honestly documented as unverified); a real plugin discovery/versioning policy (today "ABI version mismatch" just rejects — production would likely want a compatibility-range check, e.g. supporting multiple past ABI versions simultaneously); some logging/observability hook via the currently-unused `plugin_host_context`; and probably a decision about whether the trusted-in-process model is acceptable for the actual deployment, or whether the out-of-process isolation model (README "Future work") is required instead.

**64. How would you make this support untrusted plugins safely?**
Move to the out-of-process model mentioned in the README: each plugin runs in its own child process (`fork`+`exec` a small shim that loads the actual plugin library), communicating with the host over a pipe or domain socket using a serialized version of the same `init`/`execute`/`shutdown` protocol. A crash or infinite loop in a plugin then only takes down its own process (which the host can detect via `waitpid`/timeout and report, rather than the whole host process dying). That's a substantial rewrite, not an incremental change, since the whole current design assumes direct, synchronous, in-process function-pointer calls.

**65. How would you extend this to a distributed system — plugins running on different machines?**
The ABI boundary (`plugin_abi.h`) would stop being a same-process C ABI and become a network protocol instead — the `plugin_vtable`'s three operations map naturally onto three RPC methods (`Init`, `Execute`, `Shutdown`) over gRPC or a similar framework, with `plugin_metadata`'s fields becoming a registration/handshake message. The host-side lifecycle rules (`init` once, `shutdown` once idempotent, reverse-order teardown) stay conceptually identical; what changes is failure handling, since network calls can fail transiently in ways `dlopen` never does (timeout, partial failure, the plugin process restarting mid-session) — that would need retry/backoff policy this version has no need for.

**66. What's the biggest unknown risk in this codebase if it were deployed today, as-is?**
The untested Windows path, full stop — it's the one piece of platform-specific code that has never been compiled or run, anywhere, by me or by CI (GitHub Actions' matrix here only covers `macos-latest`/`ubuntu-latest`). It follows the same structural pattern as the verified POSIX path, which gives me reasonable confidence it's *roughly* right, but "roughly right by analogy" is not the same as "verified," and I say so explicitly in the README rather than claiming otherwise.

## J. Tricky cross-questions (authorship / honesty checks)

**67. Did you write every line of this yourself?**
I designed the ABI contract, the lifecycle rules, the failure taxonomy, and the test strategy, and I used an AI coding assistant as a tool while writing the implementation, the same way I'd use any other tool — I did not simply transcribe a paper design into code by hand character-by-character. What I can speak to directly, because I did the work of understanding and verifying it, is *why* every design decision in this README is what it is, what breaks if you change it, and the actual bug I found and fixed (Q58–60) — that's not something you can fake your way through in an interview without having genuinely engaged with the code.

**68. If I asked you to change the ABI to add a 64-bit capability-flags field, where exactly would you make the change, and what else would need to change with it?**
Add `uint64_t capability_flags;` to `plugin_metadata` in `plugin_abi.h`, placed after the existing fields (not before `abi_version`/`struct_size`, which must stay first per Q4). Bump `PLUGIN_ABI_VERSION` to 2, since this changes `struct_size` for every plugin and is exactly the kind of change that check exists to catch. Then update every existing example plugin and fixture's `kMetadata` initializer to include the new field (they're all aggregate-initialized positionally, so this is a required, not optional, update at every call site — `plugins/*/*.cpp` and most of `tests/fixtures/*/*.cpp`), and decide + document what capability flags actually mean and how/where `PluginManager::load()` would act on them.

**69. What's the one thing in this codebase you'd most want to revisit if you had another week?**
Actually building and testing the Windows path — right now it's "written to the same pattern as the verified POSIX path" but genuinely unverified, and that's the kind of gap that tends to hide real bugs (string encoding, `DWORD` vs `size_t` width mismatches, load-order differences in the Windows loader) that only show up once you actually run it.

**70. Someone on your team says "just use `dlopen(RTLD_GLOBAL)` everywhere, it's simpler." How do you respond?**
I'd push back: `RTLD_GLOBAL` makes a plugin's symbols visible process-wide, which means two plugins with an internal (non-exported-by-contract) same-named symbol would collide, and worse, a bug in one plugin's internal symbol resolution could silently bind to a same-named symbol in a completely unrelated plugin instead of failing loudly. `RTLD_LOCAL` (what this project actually uses) costs nothing in correctness for this use case — nothing here needs a plugin's internals visible to another plugin — and avoids that whole bug class for free.

## K. Final cheat-sheet — facts to have cold

1. ABI contract lives in exactly one file: `include/plugin_abi.h`. `PLUGIN_ABI_VERSION` is currently `1`.
2. `plugin_metadata`'s first two fields, in order, are `abi_version` then `struct_size` — checked in that order, before anything else in the struct is trusted.
3. The one required exported symbol every plugin must have: `plugin_get_metadata` (macro `PLUGIN_GET_METADATA_SYMBOL`), `extern "C"` linkage.
4. Three vtable functions: `init`, `execute`, `shutdown` — all POD function pointers, all return `plugin_status`, none may throw across the ABI.
5. `init()` is called exactly once per successful load; `shutdown()` is called exactly once, guarded by `LoadedPlugin::shutdown_once()`'s `PluginLifecycleState` check.
6. Unload order is strict LIFO (reverse of load order) — `PluginManager::unload_all()` pops `plugins_` back-to-front.
7. 9 documented `LoadError` values cover every failure mode from file-not-found through init-failed.
8. `DynamicLibrary` uses `dlopen(path, RTLD_NOW | RTLD_LOCAL)` on POSIX; `LoadLibraryA`/`GetProcAddress`/`FreeLibrary` on Windows (`#ifdef _WIN32`, **untested** on this build).
9. 3 example plugins: `reverse_string`, `math_ops`, `logging_counter`. 6 fixtures: `bad_abi_version`, `bad_struct_size`, `missing_symbol`, `init_fail`, `shutdown_counter`, `order_tracker` (built 3x as A/B/C).
10. 21 GoogleTest cases across 6 test files, all passing under plain Debug, ASan+UBSan, and ThreadSanitizer.
11. `ENABLE_ASAN` enables `-fsanitize=address,undefined` together (one combined build); `ENABLE_TSAN` is mutually exclusive with both at the CMake-configure level (`FATAL_ERROR` if combined).
12. LeakSanitizer (`detect_leaks=1`) is **not supported by ASan on macOS at all** — not a bug in this project, an Apple-platform limitation; leak checking runs on Linux CI.
13. Platform module suffix is pinned explicitly in CMake, not left to the `MODULE`-target default: `.dylib` (macOS), `.so` (Linux), `.dll` (Windows).
14. The one real bug found during this build: a test (`test_lifecycle.cpp`) called a function pointer resolved from a plugin *after* `unload_all()` had already `dlclose()`'d it — a use-after-unload segfault, fixed by reordering the test's assertions, not the host code.
15. GoogleTest is fetched via CMake `FetchContent` pinned to tag `v1.15.2` with `GIT_SHALLOW TRUE` (added specifically to work around a slow network during this build — a plain shallow `git clone` of it took ~90s on this machine).
