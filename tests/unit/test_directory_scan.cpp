#include <gtest/gtest.h>

#include "plugin_manager.hpp"

using plugin_host::LoadError;
using plugin_host::PluginManager;

TEST(DirectoryScan, AllGoodPluginsLoadFromACleanDirectory) {
    PluginManager manager;
    auto results = manager.load_directory(GOOD_PLUGIN_DIR);
    ASSERT_EQ(results.size(), 3u);
    for (const auto &r : results) {
        EXPECT_TRUE(r.success) << r.message;
    }
    EXPECT_EQ(manager.loaded_count(), 3u);
    manager.unload_all();
}

TEST(DirectoryScan, MixedDirectoryReportsEachFileWithoutAbortingTheScan) {
    PluginManager manager;
    auto results = manager.load_directory(MIXED_PLUGIN_DIR);

    // 3 good example plugins + 4 deliberately broken fixtures.
    ASSERT_EQ(results.size(), 7u);

    std::size_t successes = 0;
    std::size_t failures = 0;
    for (const auto &r : results) {
        if (r.success) {
            ++successes;
        } else {
            ++failures;
            EXPECT_NE(r.error, LoadError::None);
        }
    }
    EXPECT_EQ(successes, 3u);
    EXPECT_EQ(failures, 4u);
    EXPECT_EQ(manager.loaded_count(), 3u);

    manager.unload_all();
    EXPECT_EQ(manager.loaded_count(), 0u);
}

TEST(DirectoryScan, NonexistentDirectoryIsReportedCleanly) {
    PluginManager manager;
    auto results = manager.load_directory(std::string(MIXED_PLUGIN_DIR) + "/does/not/exist");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results.front().success);
    EXPECT_EQ(manager.loaded_count(), 0u);
}
