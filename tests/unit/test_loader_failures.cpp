#include <gtest/gtest.h>

#include "plugin_manager.hpp"

using plugin_host::LoadError;
using plugin_host::PluginManager;

TEST(LoaderFailures, FileNotFoundIsReportedCleanly) {
    PluginManager manager;
    auto result = manager.load(NONEXISTENT_PLUGIN_PATH);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, LoadError::FileNotFound);
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(LoaderFailures, MissingMetadataSymbolIsReportedCleanly) {
    PluginManager manager;
    auto result = manager.load(FIXTURE_MISSING_SYMBOL_PATH);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, LoadError::MissingMetadataSymbol);
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(LoaderFailures, AbiVersionMismatchIsReportedCleanly) {
    PluginManager manager;
    auto result = manager.load(FIXTURE_BAD_ABI_VERSION_PATH);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, LoadError::AbiVersionMismatch);
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(LoaderFailures, StructSizeMismatchIsReportedCleanly) {
    PluginManager manager;
    auto result = manager.load(FIXTURE_BAD_STRUCT_SIZE_PATH);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, LoadError::StructSizeMismatch);
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(LoaderFailures, InitFailureSurvivesAndLeavesNothingLoaded) {
    PluginManager manager;
    auto result = manager.load(FIXTURE_INIT_FAIL_PATH);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, LoadError::InitFailed);
    // The plugin_name should still have been captured from valid
    // metadata, even though init() failed afterwards.
    EXPECT_EQ(result.plugin_name, "init_fail_fixture");
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(LoaderFailures, HostSurvivesMultipleConsecutiveBadLoads) {
    // None of these should crash the process or corrupt manager state;
    // each is an independent attempt.
    PluginManager manager;
    EXPECT_FALSE(manager.load(NONEXISTENT_PLUGIN_PATH).success);
    EXPECT_FALSE(manager.load(FIXTURE_MISSING_SYMBOL_PATH).success);
    EXPECT_FALSE(manager.load(FIXTURE_BAD_ABI_VERSION_PATH).success);
    EXPECT_FALSE(manager.load(FIXTURE_BAD_STRUCT_SIZE_PATH).success);
    EXPECT_FALSE(manager.load(FIXTURE_INIT_FAIL_PATH).success);
    EXPECT_EQ(manager.loaded_count(), 0u);

    // And the manager must still work normally afterwards.
    auto good = manager.load(PLUGIN_REVERSE_STRING_PATH);
    EXPECT_TRUE(good.success);
    EXPECT_EQ(manager.loaded_count(), 1u);
}
