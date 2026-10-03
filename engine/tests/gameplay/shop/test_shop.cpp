// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/shop/loader.hpp>
#include <corundum/gameplay/shop/registry.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/ui/barter.hpp>
#include <corundum/world/flags.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace shop = corundum::gameplay::shop;
namespace ui = corundum::ui;

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
    items.add(corundum::gameplay::item::Item{.id = "trinket", .name = "Trinket"}); // price 0 -> not sellable
    return items;
  }

} // namespace

TEST_CASE("shop loader: parses stock, prices, faction and buy rate") {
  const auto dir = temp_dir("load");
  const auto path = dir / "corvin.json";
  write_file(path, R"({
    "schema_version": 1,
    "shops": [
      {
        "id": "corvin",
        "name": "Corvin's Salt",
        "faction": "village",
        "buy_rate": 0.4,
        "stock": [
          { "item": "salt", "price": 5 },
          { "item": "hammer" }
        ]
      }
    ]
  })");

  const auto result = shop::load_shop_file(path);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].id == "corvin");
  CHECK((*result)[0].faction == "village");
  CHECK((*result)[0].buy_rate == doctest::Approx(0.4f));
  REQUIRE((*result)[0].stock.size() == 2);
  CHECK((*result)[0].stock[0].item == "salt");
  CHECK((*result)[0].stock[0].price == 5);
  CHECK((*result)[0].stock[1].price == 0); // absent -> fall back to Item::price
}

TEST_CASE("shop loader: rejects out-of-range buy rate") {
  const auto dir = temp_dir("bad_rate");
  write_file(dir / "bad.json", R"({
    "schema_version": 1,
    "shops": [ { "id": "x", "name": "X", "buy_rate": 2.0 } ]
  })");
  CHECK_FALSE(shop::load_shop_file(dir / "bad.json").has_value());
}

TEST_CASE("barter price: reputation discounts buy price, clamped and floored at 1") {
  CHECK(ui::barter_buy_price(100, 0) == 100);
  CHECK(ui::barter_buy_price(100, 25) == 75);
  CHECK(ui::barter_buy_price(100, 50) == 50);
  CHECK(ui::barter_buy_price(100, 90) == 50); // clamped to +50%
  CHECK(ui::barter_buy_price(100, -20) == 100);
  CHECK(ui::barter_buy_price(1, 50) == 1); // never free
  CHECK(ui::barter_buy_price(0, 50) == 0);
}

TEST_CASE("barter sell price: scales base by buy rate") {
  CHECK(ui::barter_sell_price(10, 0.5f) == 5);
  CHECK(ui::barter_sell_price(10, 0.4f) == 4);
  CHECK(ui::barter_sell_price(10, 0.f) == 0);
  CHECK(ui::barter_sell_price(0, 0.5f) == 0);
}

TEST_CASE("build_barter_stock: explicit price overrides, absent falls back to item price") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}, shop::StockEntry{.item = "hammer"}},
  };

  const std::vector<ui::BarterLine> lines = ui::build_barter_stock(shop, items, 0);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].id == "salt");
  CHECK(lines[0].name == "Salt");
  CHECK(lines[0].unit_price == 5);
  CHECK(lines[1].unit_price == 40);

  // Reputation discounts the price: max(1, 5 - 5*50/100) = 3.
  const std::vector<ui::BarterLine> cheap = ui::build_barter_stock(shop, items, 50);
  CHECK(cheap[0].unit_price == 3);
}

TEST_CASE("build_barter_sell_lines: only priced held items, priced by buy rate") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{.buy_rate = 0.5f, .id = "corvin", .name = "Corvin's Salt"};

  corundum::world::FlagStore flags;
  corundum::world::set_flag(flags, "item.salt");
  flags["item.salt"] = 3;
  flags["item.hammer"] = 1;
  flags["item.trinket"] = 1; // price 0 -> omitted

  const std::vector<ui::BarterLine> lines = ui::build_barter_sell_lines(shop, items, flags);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].name == "Osric's Hammer");
  CHECK(lines[0].count == 1);
  CHECK(lines[0].unit_price == 20);
  CHECK(lines[1].name == "Salt");
  CHECK(lines[1].count == 3);
  CHECK(lines[1].unit_price == 5);
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
  CHECK(registry.load_all(dir) == 2);
  REQUIRE(registry.find("shared") != nullptr);
  CHECK(registry.find("shared")->name == "First");
}
