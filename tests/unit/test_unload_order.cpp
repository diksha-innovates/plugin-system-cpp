#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "plugin_manager.hpp"

using plugin_host::PluginManager;

namespace {

void set_order_log_path(const std::string &path) {
#if defined(_WIN32)
    _putenv_s("PLUGIN_ORDER_LOG_PATH", path.c_str());
#else
    setenv("PLUGIN_ORDER_LOG_PATH", path.c_str(), 1);
#endif
}

std::vector<std::string> read_lines(const std::string &path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

}  // namespace

TEST(UnloadOrder, PluginsAreShutDownInStrictReverseOfLoadOrder) {
    std::filesystem::path log_path = std::filesystem::temp_directory_path() /
                                      "plugin_system_cpp_order_log_test.txt";
    std::error_code ec;
    std::filesystem::remove(log_path, ec);
    set_order_log_path(log_path.string());

    PluginManager manager;
    ASSERT_TRUE(manager.load(FIXTURE_ORDER_TRACKER_A_PATH).success);
    ASSERT_TRUE(manager.load(FIXTURE_ORDER_TRACKER_B_PATH).success);
    ASSERT_TRUE(manager.load(FIXTURE_ORDER_TRACKER_C_PATH).success);

    manager.unload_all();

    std::vector<std::string> lines = read_lines(log_path.string());
    std::vector<std::string> expected = {
        "init:order_tracker_a", "init:order_tracker_b", "init:order_tracker_c",
        "shutdown:order_tracker_c", "shutdown:order_tracker_b", "shutdown:order_tracker_a",
    };
    EXPECT_EQ(lines, expected);

    std::filesystem::remove(log_path, ec);
}

TEST(UnloadOrder, UnloadingOneAtATimeStillRespectsStackOrder) {
    std::filesystem::path log_path = std::filesystem::temp_directory_path() /
                                      "plugin_system_cpp_order_log_test2.txt";
    std::error_code ec;
    std::filesystem::remove(log_path, ec);
    set_order_log_path(log_path.string());

    PluginManager manager;
    ASSERT_TRUE(manager.load(FIXTURE_ORDER_TRACKER_A_PATH).success);
    ASSERT_TRUE(manager.load(FIXTURE_ORDER_TRACKER_B_PATH).success);

    // Shut down (and the manager will unload on destruction) - the
    // most-recently-loaded plugin must always be shut down first.
    manager.plugin_at(1).shutdown_once();
    manager.plugin_at(0).shutdown_once();

    std::vector<std::string> lines = read_lines(log_path.string());
    std::vector<std::string> expected = {
        "init:order_tracker_a",
        "init:order_tracker_b",
        "shutdown:order_tracker_b",
        "shutdown:order_tracker_a",
    };
    EXPECT_EQ(lines, expected);

    std::filesystem::remove(log_path, ec);
}
