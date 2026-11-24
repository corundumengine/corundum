// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <doctest/doctest.h>

#include "memory_content_source.hpp"
#include "temp_dir.hpp"
#include <corundum/gameplay/item/item.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/item/loader.hpp>
#include <corundum/gameplay/item/serialize.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using corundum::test::disk;
namespace item = corundum::gameplay::item;

namespace {

void write_file(const fs::path &p, std::string_view content) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p);
  f << content;
}

corundum::test::TempDir temp_dir(std::string_view tag) {
  return corundum::test::TempDir{"crpg_test_item_serialize_", tag};
}

/** @brief Load -> serialize -> write -> load, returning the final parse. */
std::expected<std::vector<item::Item>, std::string> round_trip(const fs::path &src, item::ItemCategory category,
                                                               const fs::path &out) {
  auto loaded = item::Loader{}.load(disk(), src, category);
  if (!loaded)
    return std::unexpected(loaded.error());
  auto write_result = corundum::core::write_json(out, item::serialize(*loaded, category));
  if (!write_result)
    return std::unexpected(write_result.error());
  return item::Loader{}.load(disk(), out, category);
}

void check_same_text(const item::Item &a, const item::Item &b) {
  CHECK_EQ(a.id, b.id);
  CHECK_EQ(a.name, b.name);
  CHECK_EQ(a.description, b.description);
}

void check_same_weapon(const item::Item &a, const item::Item &b) {
  CHECK_EQ(a.weapon.has_value(), b.weapon.has_value());
  if (a.weapon && b.weapon)
    CHECK_EQ(a.weapon->damage, b.weapon->damage);
}

void check_same_apparel(const item::Item &a, const item::Item &b) {
  CHECK_EQ(a.apparel.has_value(), b.apparel.has_value());
  if (!a.apparel || !b.apparel)
    return;
  CHECK_EQ(a.apparel->slot, b.apparel->slot);
  CHECK_EQ(a.apparel->defense, b.apparel->defense);
}

void check_same_potion(const item::Item &a, const item::Item &b) {
  CHECK_EQ(a.potion.has_value(), b.potion.has_value());
  if (!a.potion || !b.potion)
    return;
  CHECK_EQ(a.potion->effect, b.potion->effect);
  CHECK_EQ(a.potion->magnitude, b.potion->magnitude);
}

void check_same_item(const item::Item &a, const item::Item &b) {
  check_same_text(a, b);
  CHECK_EQ(a.icon, b.icon);
  CHECK_EQ(a.category, b.category);
  check_same_weapon(a, b);
  check_same_apparel(a, b);
  check_same_potion(a, b);
}

} // namespace

TEST_CASE("item serialize: weapon and apparel payloads survive round-trip") {
  const auto dir = temp_dir("payloads");
  const auto src = dir / "load.json";

  // Weapon batch (damage + zero damage edge) and an apparel batch.
  write_file(src, R"({
    "schema_version": 1,
    "items": [
      { "id": "steel_sword", "name": "Steel Sword", "weapon": { "damage": 12 } },
      { "id": "blunt_sword", "name": "Blunt Sword", "weapon": { "damage": 0 } }
    ]
  })");
  auto weapons = item::Loader{}.load(disk(), src, item::ItemCategory::Weapon);
  REQUIRE(weapons.has_value());
  REQUIRE(weapons->size() == 2);
  auto weapons_roundtrip = round_trip(src, item::ItemCategory::Weapon, dir / "weapons_out.json");
  REQUIRE(weapons_roundtrip.has_value());
  REQUIRE(weapons_roundtrip->size() == 2);
  for (std::size_t i = 0; i < weapons->size(); ++i)
    check_same_item((*weapons)[i], (*weapons_roundtrip)[i]);

  write_file(src, R"({
    "schema_version": 1,
    "items": [
      { "id": "helm", "name": "Helm", "apparel": { "slot": "head", "defense": 3 } },
      { "id": "rags", "name": "Rags", "apparel": {} }
    ]
  })");
  auto apparel = item::Loader{}.load(disk(), src, item::ItemCategory::Apparel);
  REQUIRE(apparel.has_value());
  REQUIRE(apparel->size() == 2);
  auto apparel_roundtrip = round_trip(src, item::ItemCategory::Apparel, dir / "apparel_out.json");
  REQUIRE(apparel_roundtrip.has_value());
  REQUIRE(apparel_roundtrip->size() == 2);
  for (std::size_t i = 0; i < apparel->size(); ++i)
    check_same_item((*apparel)[i], (*apparel_roundtrip)[i]);
}

TEST_CASE("item serialize: emitted JSON is a valid batch document") {
  std::vector<item::Item> items;
  item::Item sword;
  sword.id = "sword";
  sword.name = "Sword";
  sword.weapon = item::WeaponData{15};
  items.push_back(sword);

  const auto j = item::serialize(items, item::ItemCategory::Weapon);
  CHECK(j.at("schema_version").get<int>() == 1);
  REQUIRE(j.at("items").is_array());
  REQUIRE(j.at("items").size() == 1);
  CHECK(j.at("items")[0].at("id").get<std::string>() == "sword");
  CHECK(j.at("items")[0].at("weapon").at("damage").get<int>() == 15);
  // The category is folder-derived; it must not appear on the element.
  CHECK_FALSE(j.at("items")[0].contains("category"));
}

TEST_CASE("item serialize: load -> serialize -> load is idempotent on the payload") {
  const auto dir = temp_dir("idempotent");
  const auto src = dir / "load.json";
  write_file(src, R"({
    "schema_version": 1,
    "items": [
      { "id": "tonic", "name": "Tonic", "potion": { "effect": "heal", "magnitude": 10 } },
      { "id": "poison", "name": "Poison", "potion": { "effect": "damage", "magnitude": 4 } }
    ]
  })");

  const auto out1 = dir / "out1.json";
  const auto out2 = dir / "out2.json";
  const auto first = round_trip(src, item::ItemCategory::Potion, out1);
  REQUIRE(first.has_value());
  const auto second = round_trip(out1, item::ItemCategory::Potion, out2);
  REQUIRE(second.has_value());

  REQUIRE(first->size() == second->size());
  for (std::size_t i = 0; i < first->size(); ++i)
    check_same_item((*first)[i], (*second)[i]);

  // The second write must be byte-identical to the first: load->save is stable.
  std::ifstream first_in(out1, std::ios::binary);
  std::ifstream second_in(out2, std::ios::binary);
  const std::string first_bytes((std::istreambuf_iterator<char>(first_in)), {});
  const std::string second_bytes((std::istreambuf_iterator<char>(second_in)), {});
  CHECK(first_bytes == second_bytes);
}