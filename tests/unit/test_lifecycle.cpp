#include <gtest/gtest.h>

#include "plugin_manager.hpp"

using plugin_host::PluginManager;
using FixtureGetCountFn = int (*)(void);

TEST(Lifecycle, ShutdownFiresExactlyOnceAcrossMultipleHostOperations) {
    PluginManager manager;
    auto result = manager.load(FIXTURE_SHUTDOWN_COUNTER_PATH);
    ASSERT_TRUE(result.success) << result.message;

    auto get_count =
        reinterpret_cast<FixtureGetCountFn>(manager.plugin_at(0).resolve_extra_symbol("fixture_get_shutdown_count"));
    ASSERT_NE(get_count, nullptr);
    EXPECT_EQ(get_count(), 0);

    // First explicit shutdown: the plugin's shutdown() runs once.
    plugin_status s1 = manager.plugin_at(0).shutdown_once();
    EXPECT_EQ(s1, PLUGIN_STATUS_OK);
    EXPECT_EQ(get_count(), 1);

    // Calling it again (simulating a host bug / defensive re-call) must
    // be a guarded no-op: the plugin's own shutdown() must NOT run a
    // second time.
    plugin_status s2 = manager.plugin_at(0).shutdown_once();
    EXPECT_EQ(s2, PLUGIN_STATUS_OK);
    EXPECT_EQ(get_count(), 1);

    // A third defensive re-call, simulating what unload_all() does
    // internally right before it dlclose()s the library, must also be
    // a no-op. This has to be checked here, while the library is still
    // mapped: get_count() is a function pointer INTO the plugin's code,
    // so calling it after unload_all() has dlclose()d the library would
    // dereference an address that is no longer mapped (undefined
    // behavior, observed as a segfault) - the counter cannot be read
    // back once the library it lives in has been unloaded.
    plugin_status s3 = manager.plugin_at(0).shutdown_once();
    EXPECT_EQ(s3, PLUGIN_STATUS_OK);
    EXPECT_EQ(get_count(), 1);

    manager.unload_all();
}

TEST(Lifecycle, ExecuteAfterShutdownFailsCleanlyInsteadOfCrashing) {
    PluginManager manager;
    auto result = manager.load(FIXTURE_SHUTDOWN_COUNTER_PATH);
    ASSERT_TRUE(result.success);

    manager.plugin_at(0).shutdown_once();

    char buffer[32];
    plugin_status status = manager.plugin_at(0).execute("x", buffer, sizeof(buffer), nullptr);
    EXPECT_EQ(status, PLUGIN_STATUS_NOT_INITIALIZED);

    manager.unload_all();
}

TEST(Lifecycle, InitFailureLeavesHostFullyFunctional) {
    PluginManager manager;
    auto bad = manager.load(FIXTURE_INIT_FAIL_PATH);
    EXPECT_FALSE(bad.success);
    EXPECT_EQ(manager.loaded_count(), 0u);

    // The host must still be able to load/use other plugins afterwards
    // (nothing was left in a half-broken state by the failed load).
    auto good = manager.load(PLUGIN_REVERSE_STRING_PATH);
    ASSERT_TRUE(good.success);
    char buffer[32];
    EXPECT_EQ(manager.plugin_at(0).execute("ok", buffer, sizeof(buffer), nullptr), PLUGIN_STATUS_OK);
    EXPECT_STREQ(buffer, "ko");
    manager.unload_all();
}
