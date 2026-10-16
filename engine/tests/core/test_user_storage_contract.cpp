// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "memory_user_storage.hpp"
#include "temp_dir.hpp"

#include <corundum/core/user_storage.hpp>

#include <string_view>

namespace {

  // One flat list of contract assertions; splitting it would hide the shared contract.
  // NOLINTNEXTLINE(readability-function-size)
  void check_contract(corundum::core::UserStorage &storage, std::string_view label) {
    INFO(label);

    CHECK_FALSE(storage.contains("saves/slot_01.json"));

    REQUIRE(storage.write("saves/slot_01.json", "first").has_value());
    CHECK(storage.contains("saves/slot_01.json"));

    const auto first = storage.read("saves/slot_01.json");
    REQUIRE(first.has_value());
    CHECK_EQ(*first, "first");

    REQUIRE(storage.write("saves/slot_01.json", "second").has_value());
    const auto second = storage.read("saves/slot_01.json");
    REQUIRE(second.has_value());
    CHECK_EQ(*second, "second");

    CHECK_FALSE(storage.read("missing.json").has_value());
  }

} // namespace

TEST_CASE("UserStorage contract: DirectoryUserStorage") {
  const auto dir = corundum::test::TempDir{"crpg_test_user_storage_contract_", "disk"};
  corundum::core::DirectoryUserStorage storage(dir.path());
  check_contract(storage, "DirectoryUserStorage");
}

TEST_CASE("UserStorage contract: MemoryUserStorage") {
  corundum::test::MemoryUserStorage storage;
  check_contract(storage, "MemoryUserStorage");
}

TEST_CASE("MemoryUserStorage: injected_write_error is returned and nothing is stored") {
  corundum::test::MemoryUserStorage storage;
  storage.injected_write_error = "disk full";

  const auto result = storage.write("a.json", "data");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), "disk full");
  CHECK_FALSE(storage.contains("a.json"));
}
