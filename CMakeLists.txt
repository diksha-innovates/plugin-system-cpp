cmake_minimum_required(VERSION 3.16)
project(plugin_system_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(PLUGIN_SYSTEM_BUILD_TESTS "Build the GoogleTest test suite" ON)

# This project's host/plugin interaction is single-threaded (the host
# loads, initializes, calls, and shuts down plugins sequentially from a
# single thread; see README "Limitations"). ENABLE_TSAN is provided for
# completeness/consistency with the other sanitizer options and the CI
# matrix pattern, but it is not expected to find anything because there
# is no concurrency to race on.
if(ENABLE_TSAN AND (ENABLE_ASAN OR ENABLE_UBSAN))
  message(FATAL_ERROR "ThreadSanitizer cannot be combined with ASan/UBSan in the same binary")
endif()

# Warnings-as-errors apply to OUR code only (host, plugins, fixtures,
# tests). Third-party code pulled in via FetchContent (GoogleTest) is
# built with its own settings below, deliberately not inheriting these.
add_library(plugin_system_warnings INTERFACE)
target_compile_options(plugin_system_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror
)

add_library(plugin_system_sanitizers INTERFACE)
if(ENABLE_ASAN)
  target_compile_options(plugin_system_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(plugin_system_sanitizers INTERFACE -fsanitize=address,undefined)
elseif(ENABLE_UBSAN)
  target_compile_options(plugin_system_sanitizers INTERFACE -fsanitize=undefined -fno-omit-frame-pointer -g)
  target_link_options(plugin_system_sanitizers INTERFACE -fsanitize=undefined)
elseif(ENABLE_TSAN)
  target_compile_options(plugin_system_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer -g)
  target_link_options(plugin_system_sanitizers INTERFACE -fsanitize=thread)
endif()

# ---------------------------------------------------------------------
# Host library (RAII loader + plugin manager) and CLI executable
# ---------------------------------------------------------------------
add_library(plugin_host_lib STATIC
  src/host/dynamic_library.cpp
  src/host/plugin_manager.cpp
)
target_include_directories(plugin_host_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/host
)
target_link_libraries(plugin_host_lib PUBLIC plugin_system_sanitizers)
target_link_libraries(plugin_host_lib PRIVATE plugin_system_warnings)
if(NOT WIN32)
  target_link_libraries(plugin_host_lib PUBLIC ${CMAKE_DL_LIBS})
endif()

add_executable(plugin_host src/host/main.cpp)
target_link_libraries(plugin_host PRIVATE plugin_host_lib plugin_system_warnings)

# ---------------------------------------------------------------------
# Example plugins - each its own shared library target, POD-only ABI
# ---------------------------------------------------------------------
# MODULE libraries default to CMAKE_SHARED_MODULE_SUFFIX, which is not
# guaranteed to be ".dylib" on Apple platforms (historically loadable
# modules used ".so" there too). The spec requires the host to look for
# .dylib on macOS and .so on Linux, so the suffix is pinned explicitly.
if(APPLE)
  set(PLUGIN_MODULE_SUFFIX ".dylib")
elseif(WIN32)
  set(PLUGIN_MODULE_SUFFIX ".dll")
else()
  set(PLUGIN_MODULE_SUFFIX ".so")
endif()

function(add_example_plugin target_name source_file)
  add_library(${target_name} MODULE ${source_file})
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(${target_name} PRIVATE plugin_system_warnings plugin_system_sanitizers)
  set_target_properties(${target_name} PROPERTIES PREFIX "" SUFFIX "${PLUGIN_MODULE_SUFFIX}")
endfunction()

add_example_plugin(reverse_string plugins/reverse_string/reverse_string_plugin.cpp)
add_example_plugin(math_ops plugins/math_ops/math_ops_plugin.cpp)
add_example_plugin(logging_counter plugins/logging_counter/logging_counter_plugin.cpp)

# ---------------------------------------------------------------------
# Broken test fixtures - deliberately exercise failure paths
# ---------------------------------------------------------------------
function(add_fixture_plugin target_name source_file)
  add_library(${target_name} MODULE ${source_file})
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(${target_name} PRIVATE plugin_system_warnings plugin_system_sanitizers)
  set_target_properties(${target_name} PROPERTIES PREFIX "" SUFFIX "${PLUGIN_MODULE_SUFFIX}")
endfunction()

add_fixture_plugin(fixture_bad_abi_version tests/fixtures/bad_abi_version/bad_abi_version_plugin.cpp)
add_fixture_plugin(fixture_bad_struct_size tests/fixtures/bad_struct_size/bad_struct_size_plugin.cpp)
add_fixture_plugin(fixture_missing_symbol tests/fixtures/missing_symbol/missing_symbol_plugin.cpp)
add_fixture_plugin(fixture_init_fail tests/fixtures/init_fail/init_fail_plugin.cpp)
add_fixture_plugin(fixture_shutdown_counter tests/fixtures/shutdown_counter/shutdown_counter_plugin.cpp)

add_library(fixture_order_tracker_a MODULE tests/fixtures/order_tracker/order_tracker_plugin.cpp)
add_library(fixture_order_tracker_b MODULE tests/fixtures/order_tracker/order_tracker_plugin.cpp)
add_library(fixture_order_tracker_c MODULE tests/fixtures/order_tracker/order_tracker_plugin.cpp)
foreach(suffix a b c)
  string(TOUPPER ${suffix} suffix_upper)
  target_compile_definitions(fixture_order_tracker_${suffix} PRIVATE ORDER_TRACKER_NAME="order_tracker_${suffix}")
  target_include_directories(fixture_order_tracker_${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(fixture_order_tracker_${suffix} PRIVATE plugin_system_warnings plugin_system_sanitizers)
  set_target_properties(fixture_order_tracker_${suffix} PROPERTIES PREFIX "" SUFFIX "${PLUGIN_MODULE_SUFFIX}")
endforeach()

# A directory containing ONLY good plugins, assembled at build time via
# generator-expression copies, used by the directory-scan tests/demo so
# the scan doesn't pick up fixtures (or vice versa) by accident.
set(GOOD_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/good_plugins)
set(MIXED_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/mixed_plugins)
foreach(tgt reverse_string math_ops logging_counter)
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GOOD_PLUGIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${tgt}> ${GOOD_PLUGIN_DIR}/
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MIXED_PLUGIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${tgt}> ${MIXED_PLUGIN_DIR}/
  )
endforeach()
foreach(tgt fixture_bad_abi_version fixture_bad_struct_size fixture_missing_symbol fixture_init_fail)
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MIXED_PLUGIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${tgt}> ${MIXED_PLUGIN_DIR}/
  )
endforeach()

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
if(PLUGIN_SYSTEM_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(plugin_system_tests
    tests/unit/test_abi_layout.cpp
    tests/unit/test_loader_success.cpp
    tests/unit/test_loader_failures.cpp
    tests/unit/test_lifecycle.cpp
    tests/unit/test_directory_scan.cpp
    tests/unit/test_unload_order.cpp
  )
  target_include_directories(plugin_system_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(plugin_system_tests PRIVATE plugin_host_lib gtest_main plugin_system_warnings)
  target_compile_definitions(plugin_system_tests PRIVATE
    GOOD_PLUGIN_DIR="${GOOD_PLUGIN_DIR}"
    MIXED_PLUGIN_DIR="${MIXED_PLUGIN_DIR}"
    PLUGIN_REVERSE_STRING_PATH="$<TARGET_FILE:reverse_string>"
    PLUGIN_MATH_OPS_PATH="$<TARGET_FILE:math_ops>"
    PLUGIN_LOGGING_COUNTER_PATH="$<TARGET_FILE:logging_counter>"
    FIXTURE_BAD_ABI_VERSION_PATH="$<TARGET_FILE:fixture_bad_abi_version>"
    FIXTURE_BAD_STRUCT_SIZE_PATH="$<TARGET_FILE:fixture_bad_struct_size>"
    FIXTURE_MISSING_SYMBOL_PATH="$<TARGET_FILE:fixture_missing_symbol>"
    FIXTURE_INIT_FAIL_PATH="$<TARGET_FILE:fixture_init_fail>"
    FIXTURE_SHUTDOWN_COUNTER_PATH="$<TARGET_FILE:fixture_shutdown_counter>"
    FIXTURE_ORDER_TRACKER_A_PATH="$<TARGET_FILE:fixture_order_tracker_a>"
    FIXTURE_ORDER_TRACKER_B_PATH="$<TARGET_FILE:fixture_order_tracker_b>"
    FIXTURE_ORDER_TRACKER_C_PATH="$<TARGET_FILE:fixture_order_tracker_c>"
    NONEXISTENT_PLUGIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/does_not_exist.so"
  )
  add_dependencies(plugin_system_tests
    reverse_string math_ops logging_counter
    fixture_bad_abi_version fixture_bad_struct_size fixture_missing_symbol fixture_init_fail
    fixture_shutdown_counter fixture_order_tracker_a fixture_order_tracker_b fixture_order_tracker_c
  )

  include(GoogleTest)
  gtest_discover_tests(plugin_system_tests)
endif()
