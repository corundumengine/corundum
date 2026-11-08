// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "gameplay/rules/flat_ruleset.hpp"
#include "memory_content_source.hpp"
#include "temp_dir.hpp"
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/gameplay/shop/loader.hpp>
#include <corundum/gameplay/shop/registry.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/world/flags.hpp>

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
    items.add(corundum::gameplay::item::Item{.id = "trinket", .name = "Trinket"}); // price 0 -> not sellable
    return items;
  }

} // namespace

TEST_CASE("shop loader: parses stock, prices and faction") {
  const auto dir = temp_dir("load");
  const auto path = dir / "corvin.json";
  write_file(path, R"({
    "schema_version": 1,
    "shops": [
      {
        "id": "corvin",
        "name": "Corvin's Salt",
        "faction": "village",
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
  CHECK((*result)[0].faction == "village");
  REQUIRE((*result)[0].stock.size() == 2);
  CHECK((*result)[0].stock[0].item == "salt");
  CHECK((*result)[0].stock[0].price == 5);
  CHECK((*result)[0].stock[1].price == 0); // absent -> fall back to Item::price
}

TEST_CASE("build_barter_stock: explicit price overrides, absent falls back to item price") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}, shop::StockEntry{.item = "hammer"}},
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items, {}, corundum::test::make_flat_ruleset());
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].id == "salt");
  CHECK(lines[0].name == "Salt");
  CHECK(lines[0].unit_price == 5);
  CHECK(lines[1].unit_price == 40);
}

TEST_CASE("build_barter_stock: buy_price sees the base price and the shop faction's standing") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .faction = "village",
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}},
  };
  corundum::world::FlagStore flags;
  flags["rep.village"] = 4;

  const corundum::gameplay::rules::Ruleset ruleset{
      .buy_price = [](const corundum::gameplay::rules::FlagReader &,
                      const corundum::gameplay::rules::TradeQuery &query) { return query.base_price - query.standing; },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items, flags, ruleset);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].unit_price == 1);
}

TEST_CASE("build_barter_stock: a shop with no faction passes standing 0") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}},
  };
  corundum::world::FlagStore flags;
  flags["rep.village"] = 4;

  const corundum::gameplay::rules::Ruleset ruleset{
      .buy_price = [](const corundum::gameplay::rules::FlagReader &,
                      const corundum::gameplay::rules::TradeQuery &query) { return query.base_price - query.standing; },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items, flags, ruleset);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].unit_price == 5);
}

TEST_CASE("build_barter_stock: buy_price sees each row's base price") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .faction = "village",
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}, shop::StockEntry{.item = "hammer"}},
  };
  corundum::world::FlagStore flags;
  flags["rep.village"] = 2;

  std::vector<corundum::gameplay::rules::TradeQuery> queries;
  const corundum::gameplay::rules::Ruleset ruleset{
      .buy_price =
          [&queries](const corundum::gameplay::rules::FlagReader &,
                     const corundum::gameplay::rules::TradeQuery &query) {
            queries.push_back(query);
            return query.base_price;
          },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items, flags, ruleset);

  REQUIRE(lines.size() == 2);
  REQUIRE(queries.size() == 2);
  CHECK(queries[0].base_price == 5);
  CHECK(queries[0].standing == 2);
  CHECK(queries[1].base_price == 40);
  CHECK(queries[1].standing == 2);
}

TEST_CASE("build_barter_stock: a positive base price never costs less than 1 gold") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}, shop::StockEntry{.item = "trinket"}},
  };

  const corundum::gameplay::rules::Ruleset ruleset{
      .buy_price = [](const corundum::gameplay::rules::FlagReader &,
                      const corundum::gameplay::rules::TradeQuery &) { return 0; },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items, {}, ruleset);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].unit_price == 1);
  CHECK(lines[1].unit_price == 0); // base price 0 stays 0
}

TEST_CASE("build_barter_stock: no buy_price callback keeps the base price") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{
      .id = "corvin",
      .name = "Corvin's Salt",
      .stock = {shop::StockEntry{.item = "salt", .price = 5}, shop::StockEntry{.item = "hammer"}},
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_stock(shop, items, {}, corundum::gameplay::rules::Ruleset{});
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].unit_price == 5);
  CHECK(lines[1].unit_price == 40);
}

TEST_CASE("build_barter_sell_lines: only priced held items, priced by the ruleset") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{.id = "corvin", .name = "Corvin's Salt"};

  corundum::world::FlagStore flags;
  corundum::world::set_flag(flags, "item.salt");
  flags["item.salt"] = 3;
  flags["item.hammer"] = 1;
  flags["item.trinket"] = 1; // price 0 -> omitted

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_sell_lines(shop, items, flags, corundum::test::make_flat_ruleset());
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].name == "Osric's Hammer");
  CHECK(lines[0].count == 1);
  CHECK(lines[0].unit_price == 20);
  CHECK(lines[1].name == "Salt");
  CHECK(lines[1].count == 3);
  CHECK(lines[1].unit_price == 5);
}

TEST_CASE("build_barter_sell_lines: sell_price sees the base price and the shop faction's standing") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{.faction = "village", .id = "corvin", .name = "Corvin's Salt"};
  corundum::world::FlagStore flags;
  flags["item.salt"] = 1;
  flags["rep.village"] = 4;

  const corundum::gameplay::rules::Ruleset ruleset{
      .sell_price =
          [](const corundum::gameplay::rules::FlagReader &, const corundum::gameplay::rules::TradeQuery &query) {
            return query.base_price + query.standing;
          },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_sell_lines(shop, items, flags, ruleset);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].unit_price == 14);
}

TEST_CASE("build_barter_sell_lines: a shop with no faction passes standing 0") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{.id = "corvin", .name = "Corvin's Salt"};
  corundum::world::FlagStore flags;
  flags["item.salt"] = 1;
  flags["rep.village"] = 4;

  const corundum::gameplay::rules::Ruleset ruleset{
      .sell_price =
          [](const corundum::gameplay::rules::FlagReader &, const corundum::gameplay::rules::TradeQuery &query) {
            return query.base_price + query.standing;
          },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_sell_lines(shop, items, flags, ruleset);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].unit_price == 10);
}

TEST_CASE("build_barter_sell_lines: no sell_price callback means no rows") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{.id = "corvin", .name = "Corvin's Salt"};
  corundum::world::FlagStore flags;
  flags["item.salt"] = 1;

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_sell_lines(shop, items, flags, corundum::gameplay::rules::Ruleset{});
  CHECK(lines.empty());
}

TEST_CASE("build_barter_sell_lines: a sell_price of 0 omits only that row") {
  const corundum::gameplay::item::Registry items = make_items();
  const shop::Shop shop{.id = "corvin", .name = "Corvin's Salt"};
  corundum::world::FlagStore flags;
  flags["item.salt"] = 1;
  flags["item.hammer"] = 1;

  const corundum::gameplay::rules::Ruleset ruleset{
      .sell_price =
          [](const corundum::gameplay::rules::FlagReader &, const corundum::gameplay::rules::TradeQuery &query) {
            return query.base_price == 10 ? 0 : query.base_price;
          },
  };

  const std::vector<corundum::gameplay::screens::BarterLine> lines =
      corundum::gameplay::screens::build_barter_sell_lines(shop, items, flags, ruleset);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].name == "Osric's Hammer");
  CHECK(lines[0].unit_price == 40);
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
