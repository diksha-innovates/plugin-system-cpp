#include <gtest/gtest.h>

#include "plugin_manager.hpp"

using plugin_host::LoadError;
using plugin_host::PluginManager;

TEST(LoaderSuccess, ReverseStringLoadsAndExecutesAndUnloads) {
    PluginManager manager;
    auto result = manager.load(PLUGIN_REVERSE_STRING_PATH);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.error, LoadError::None);
    EXPECT_EQ(result.plugin_name, "reverse_string");
    ASSERT_EQ(manager.loaded_count(), 1u);
    EXPECT_EQ(manager.plugin_at(0).name(), "reverse_string");
    EXPECT_EQ(manager.plugin_at(0).version(), "1.0.0");

    char buffer[64];
    size_t written = 0;
    plugin_status status = manager.plugin_at(0).execute("hello", buffer, sizeof(buffer), &written);
    EXPECT_EQ(status, PLUGIN_STATUS_OK);
    EXPECT_STREQ(buffer, "olleh");
    EXPECT_EQ(written, 5u);

    manager.unload_all();
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(LoaderSuccess, MathOpsLoadsAndComputes) {
    PluginManager manager;
    auto result = manager.load(PLUGIN_MATH_OPS_PATH);
    ASSERT_TRUE(result.success) << result.message;

    char buffer[64];
    plugin_status status = manager.plugin_at(0).execute("add,3,4", buffer, sizeof(buffer), nullptr);
    EXPECT_EQ(status, PLUGIN_STATUS_OK);
    EXPECT_STREQ(buffer, "7.000000");

    status = manager.plugin_at(0).execute("div,10,0", buffer, sizeof(buffer), nullptr);
    EXPECT_EQ(status, PLUGIN_STATUS_INVALID_ARGUMENT);

    manager.unload_all();
}

TEST(LoaderSuccess, LoggingCounterTracksPerInstanceState) {
    PluginManager manager;
    auto result = manager.load(PLUGIN_LOGGING_COUNTER_PATH);
    ASSERT_TRUE(result.success) << result.message;

    char buffer[128];
    ASSERT_EQ(manager.plugin_at(0).execute("a", buffer, sizeof(buffer), nullptr), PLUGIN_STATUS_OK);
    EXPECT_STREQ(buffer, "count=1 last_input=a");
    ASSERT_EQ(manager.plugin_at(0).execute("b", buffer, sizeof(buffer), nullptr), PLUGIN_STATUS_OK);
    EXPECT_STREQ(buffer, "count=2 last_input=b");

    manager.unload_all();
}

TEST(LoaderSuccess, MetadataIsReadableBeforeExecute) {
    PluginManager manager;
    auto result = manager.load(PLUGIN_REVERSE_STRING_PATH);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.plugin_name, "reverse_string");
    EXPECT_EQ(manager.plugin_at(0).path(), std::string(PLUGIN_REVERSE_STRING_PATH));
}
