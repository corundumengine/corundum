// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "memory_content_source.hpp"
#include "temp_dir.hpp"
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/gameplay/shop/loader.hpp>
#include <corundum/gameplay/shop/registry.hpp>
#include <corundum/gameplay/shop/shop.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

using corundum::test::disk;

namespace fs = std::filesystem;
namespace shop = corundum::gameplay::shop;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_shop_", tag};
  }

  corundum::gameplay::item::Registry make_items() {
    corundum::gameplay::item::Registry items;
    items.add(corundum::gameplay::item::Item{.id = "salt", .name = "Salt", .price = 10});
    items.add(corundum::gameplay::item::Item{.id = "hammer", .name = "Osric's Hammer", .price = 40});
    return items;
  }

} // namespace

TEST_CASE("shop loader: parses stock and prices") {
  const auto dir = temp_dir("load");
  const auto path = dir / "corvin.json";
  write_file(path, R"({
    "schema_version": 1,
    "shops": [
      {
        "id": "corvin",
        "name": "Corvin's Salt",
        "stock": [
          { "item": "salt", "price": 5 },
          { "item": "hammer" }
        ]
      }
    ]
  })");

  const auto result = shop::load_shop_file(disk(), path);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].id == "corvin");
  REQUIRE((*result)[0].stock.size() == 2);
  CHECK((*result)[0].stock[0].item == "salt");
  CHECK((*result)[0].stock[0].price == 5);
  CHECK((*result)[0].stock[1].price == 0); // absent -> fall back to Item::price
}

TEST_CASE("shop loader: an old file's faction key is ignored") {
  const auto dir = temp_dir("old_faction");
  const auto path = dir / "corvin.json";
  write_file(path, R"({
    "schema_version": 1,
    "shops": [
      { "id": "corvin", "name": "Corvin's Salt", "faction": "village", "stock": [ { "item": "salt" } ] }
    ]
  })");

  const auto result = shop::load_shop_file(disk(), path);
  REQUIRE(result.has_value());
  REQUIRE_EQ(result->size(), 1);
  CHECK_EQ((*result)[0].id, "corvin");
  CHECK_EQ((*result)[0].name, "Corvin's Salt");
  REQUIRE_EQ((*result)[0].stock.size(), 1);
  CHECK_EQ((*result)[0].stock[0].item, "salt");
}

TEST_CASE("build_barter_stock: explicit price overrides, absent falls back to item price") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}, shop::StockEntry{.item = "hammer"}},
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].id == "salt");
  CHECK(lines[0].name == "Salt");
  CHECK(lines[0].unit_price == 5);
  CHECK(lines[1].unit_price == 40);
}

TEST_CASE("shop registry: loads a directory and skips duplicate ids") {
  const auto dir = temp_dir("registry");
  write_file(dir / "a.json", R"({
    "schema_version": 1,
    "shops": [ { "id": "a", "name": "A" }, { "id": "shared", "name": "First" } ]
  })");
  write_file(dir / "b.json", R"({
    "schema_version": 1,
    "shops": [ { "id": "shared", "name": "Second" } ]
  })");

  shop::Registry registry;
  CHECK(registry.load_all(disk(), dir) == 2);
  REQUIRE(registry.find("shared") != nullptr);
  CHECK(registry.find("shared")->name == "First");
}

TEST_CASE("shop::Registry: load_all reads shops from an in-memory content source") {
  corundum::test::MemoryContentSource source;
  source.add("shops/a.json", R"({"schema_version":1,"shops":[{"id":"a","name":"A"}]})");

  shop::Registry registry;
  CHECK(registry.load_all(source, "shops") == 1);
}
