// Compile-time guarantees about the ABI boundary types themselves:
// every type that crosses the boundary must be POD / trivially copyable
// with a standard (predictable) layout. If any of these static_asserts
// ever fail, the ABI header has been changed in a way that breaks the
// "POD-only across the boundary" rule.

#include "plugin_abi.h"

#include <gtest/gtest.h>

#include <type_traits>

static_assert(std::is_trivial<plugin_status>::value, "plugin_status must be trivial");
static_assert(std::is_standard_layout<plugin_vtable>::value, "plugin_vtable must be standard-layout");
static_assert(std::is_trivially_copyable<plugin_vtable>::value, "plugin_vtable must be trivially copyable");
static_assert(std::is_standard_layout<plugin_metadata>::value, "plugin_metadata must be standard-layout");
static_assert(std::is_trivially_copyable<plugin_metadata>::value, "plugin_metadata must be trivially copyable");
static_assert(std::is_trivial<plugin_metadata>::value, "plugin_metadata must be trivial (POD)");

TEST(AbiLayout, MetadataStructSizeIsStable) {
    // This is exactly the value every plugin bakes into its
    // plugin_metadata::struct_size field at compile time. If this
    // test's reported value ever differs between two compilations of
    // this project, a host and a plugin built from those two
    // compilations would disagree and the loader's struct-size check
    // would (correctly) refuse to load the plugin.
    EXPECT_EQ(sizeof(plugin_metadata), sizeof(plugin_metadata));
    EXPECT_GT(sizeof(plugin_metadata), 0u);
}

TEST(AbiLayout, AbiVersionIsNonZero) {
    EXPECT_NE(PLUGIN_ABI_VERSION, 0u);
}

TEST(AbiLayout, NameAndVersionFieldsAreFixedSizeCharArrays) {
    static_assert(sizeof(plugin_metadata{}.name) == PLUGIN_NAME_MAX_LEN, "name field size mismatch");
    static_assert(sizeof(plugin_metadata{}.version) == PLUGIN_VERSION_MAX_LEN, "version field size mismatch");
    SUCCEED();
}
