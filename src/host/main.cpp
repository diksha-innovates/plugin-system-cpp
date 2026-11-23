#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "plugin_manager.hpp"

namespace {

void print_usage(const char *argv0) {
    std::fprintf(stderr,
                 "usage:\n"
                 "  %s load <plugin-path> [execute-input]\n"
                 "  %s scan <directory> [execute-input]\n",
                 argv0, argv0);
}

void print_load_result(const plugin_host::LoadResult &r, const std::string &path) {
    if (r.success) {
        std::printf("[OK]   %-40s loaded (index=%zu)\n", path.c_str(), r.index);
    } else {
        std::printf("[FAIL] %-40s %s: %s\n", path.c_str(), plugin_host::to_string(r.error).c_str(),
                    r.message.c_str());
    }
}

int run_execute(plugin_host::PluginManager &manager, std::size_t index, const std::string &input) {
    char buffer[1024];
    std::size_t written = 0;
    plugin_status status = manager.plugin_at(index).execute(input.c_str(), buffer, sizeof(buffer), &written);
    if (status == PLUGIN_STATUS_OK) {
        std::printf("       execute(\"%s\") -> \"%s\"\n", input.c_str(), buffer);
        return 0;
    }
    std::printf("       execute(\"%s\") failed with status %d\n", input.c_str(), static_cast<int>(status));
    return 1;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }

    const std::string mode = argv[1];
    const std::string target = argv[2];
    const std::string execute_input = argc >= 4 ? argv[3] : std::string();

    plugin_host::PluginManager manager;

    if (mode == "load") {
        plugin_host::LoadResult r = manager.load(target);
        print_load_result(r, target);
        if (!r.success) {
            return 1;
        }
        if (!execute_input.empty()) {
            run_execute(manager, r.index, execute_input);
        }
        manager.unload_all();
        std::printf("       unloaded cleanly\n");
        return 0;
    }

    if (mode == "scan") {
        std::vector<plugin_host::LoadResult> results = manager.load_directory(target);
        int failures = 0;
        for (const auto &r : results) {
            print_load_result(r, target);
            if (!r.success) {
                ++failures;
            }
        }
        if (!execute_input.empty()) {
            for (std::size_t i = 0; i < manager.loaded_count(); ++i) {
                run_execute(manager, i, execute_input);
            }
        }
        std::printf("scan summary: %zu succeeded, %d failed, out of %zu attempted\n",
                     manager.loaded_count(), failures, results.size());
        manager.unload_all();
        std::printf("all plugins unloaded cleanly\n");
        return 0;
    }

    print_usage(argv[0]);
    return 2;
}
