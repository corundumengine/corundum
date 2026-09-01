#include <doctest/doctest.h>

#include <corundum/item/loader.hpp>
#include <corundum/item/registry.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;
namespace item = corundum::item;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_items" / tag;
    fs::create_directories(p);
    return p;
  }

} // namespace

TEST_CASE("item loader: batch file loads multiple items and injects the folder category") {
  const auto dir = temp_dir("batch");
  const auto path = dir / "steel.json";
  write_file(path, R"({
    "schema_version": 1,
    "items": [
      { "id": "steel_sword", "name": "Steel Sword", "weapon": { "damage": 12 } },
      { "id": "steel_axe", "name": "Steel Axe", "weapon": { "damage": 18 } }
    ]
  })");

  const auto result = item::load_item_file(path, item::ItemCategory::Weapon);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 2);

  const auto &sword = (*result)[0];
  CHECK(sword.id == "steel_sword");
  CHECK(sword.name == "Steel Sword");
  CHECK(sword.category == item::ItemCategory::Weapon);
  REQUIRE(sword.weapon.has_value());
  CHECK(sword.weapon->damage == 12);

  const auto &axe = (*result)[1];
  CHECK(axe.id == "steel_axe");
  CHECK(axe.name == "Steel Axe");
  CHECK(axe.category == item::ItemCategory::Weapon);
  REQUIRE(axe.weapon.has_value());
  CHECK(axe.weapon->damage == 18);
}

TEST_CASE("item loader: item missing id fails schema validation for that element only") {
  const auto dir = temp_dir("missing_id");
  const auto path = dir / "batch.json";
  write_file(path, R"({
    "schema_version": 1,
    "items": [
      { "id": "good", "name": "Good" },
      { "name": "No Id" }
    ]
  })");

  const auto result = item::load_item_file(path, item::ItemCategory::Misc);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].id == "good");
}

TEST_CASE("item loader: explicit conflicting category is overridden by the folder") {
  const auto dir = temp_dir("conflicting_category");
  const auto path = dir / "batch.json";
  write_file(path, R"({
    "schema_version": 1,
    "items": [
      { "id": "misc_item", "name": "Misc Item", "category": "misc", "weapon": { "damage": 5 } }
    ]
  })");

  // Folder says Weapon; the explicit "category": "misc" must be overridden and
  // the item still loads (with a warning on stderr). The "weapon" payload is
  // required because the injected category is Weapon.
  const auto result = item::load_item_file(path, item::ItemCategory::Weapon);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].id == "misc_item");
  CHECK((*result)[0].category == item::ItemCategory::Weapon);
}

TEST_CASE("item loader: apparel and potion payloads parse into the Item struct") {
  const auto dir = temp_dir("categories");
  const auto path = dir / "batch.json";
  write_file(path, R"({
    "schema_version": 1,
    "items": [
      {
        "id": "helm",
        "name": "Helm",
        "apparel": { "slot": "head", "defense": 3 },
        "potion": { "effect": "heal", "magnitude": 10 }
      }
    ]
  })");

  const auto result = item::load_item_file(path, item::ItemCategory::Apparel);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);

  const auto &helm = (*result)[0];
  CHECK(helm.id == "helm");
  CHECK(helm.category == item::ItemCategory::Apparel);
  REQUIRE(helm.apparel.has_value());
  CHECK(helm.apparel->slot == "head");
  CHECK(helm.apparel->defense == 3);
  // The "potion" object is present but the injected category is Apparel — the
  // loader only fills the payload matching the declared category.
  CHECK_FALSE(helm.potion.has_value());
}

TEST_CASE("item loader: category folder mapping round-trips") {
  CHECK(item::category_dir_name(item::ItemCategory::Weapon) == "weapons");
  CHECK(item::category_dir_name(item::ItemCategory::Apparel) == "apparel");
  CHECK(item::category_dir_name(item::ItemCategory::Potion) == "potions");
  CHECK(item::category_dir_name(item::ItemCategory::Misc) == "misc");

  CHECK(item::category_from_dir_name("weapons") == item::ItemCategory::Weapon);
  CHECK(item::category_from_dir_name("apparel") == item::ItemCategory::Apparel);
  CHECK(item::category_from_dir_name("potions") == item::ItemCategory::Potion);
  CHECK(item::category_from_dir_name("misc") == item::ItemCategory::Misc);
  CHECK(item::category_from_dir_name("garbage") == std::nullopt);
}

TEST_CASE("item loader: missing schema_version fails") {
  const auto dir = temp_dir("missing_schema_version");
  const auto path = dir / "batch.json";
  write_file(path, R"({"items":[]})");

  const auto result = item::load_item_file(path, item::ItemCategory::Misc);
  CHECK(!result.has_value());
}

TEST_CASE("item loader: wrong-type schema_version fails") {
  const auto dir = temp_dir("wrong_type_schema_version");
  const auto path = dir / "batch.json";
  write_file(path, R"({"schema_version":"one","items":[]})");

  const auto result = item::load_item_file(path, item::ItemCategory::Misc);
  CHECK(!result.has_value());
}

TEST_CASE("item loader: unsupported schema_version fails") {
  const auto dir = temp_dir("unsupported_schema_version");
  const auto path = dir / "batch.json";
  write_file(path, R"({"schema_version":2,"items":[]})");

  const auto result = item::load_item_file(path, item::ItemCategory::Misc);
  CHECK(!result.has_value());
  CHECK(result.error().find("1") != std::string::npos);
}

TEST_CASE("item loader: non-array items fails") {
  const auto dir = temp_dir("items_not_array");
  const auto path = dir / "batch.json";
  write_file(path, R"({"schema_version":1,"items":{}})");

  const auto result = item::load_item_file(path, item::ItemCategory::Misc);
  CHECK(!result.has_value());
}

TEST_CASE("item loader: malformed JSON returns error") {
  const auto dir = temp_dir("malformed");
  const auto path = dir / "batch.json";
  write_file(path, "not valid json");

  const auto result = item::load_item_file(path, item::ItemCategory::Misc);
  CHECK(!result.has_value());
  CHECK(result.error().find("malformed item JSON") != std::string::npos);
}

TEST_CASE("item loader: weapon folder item missing weapon object fails schema validation") {
  const auto dir = temp_dir("weapon_no_payload");
  const auto path = dir / "batch.json";
  write_file(path, R"({
    "schema_version": 1,
    "items": [
      { "id": "sword", "name": "Sword" }
    ]
  })");

  // Injected category is Weapon, so the missing "weapon" object fails schema
  // validation for that element only — the file still loads (with zero items).
  const auto result = item::load_item_file(path, item::ItemCategory::Weapon);
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("registry load_all loads category folders, skips unknown and bad files") {
  const auto dir = temp_dir("registry");

  // weapons/steel.json — two items, no explicit category.
  write_file(dir / "weapons" / "steel.json", R"({
    "schema_version": 1,
    "items": [
      { "id": "steel_sword", "name": "Steel Sword", "weapon": { "damage": 12 } },
      { "id": "steel_axe", "name": "Steel Axe", "weapon": { "damage": 18 } }
    ]
  })");
  // potions/healing.json — one item.
  write_file(dir / "potions" / "healing.json", R"({
    "schema_version": 1,
    "items": [
      { "id": "heal_flask", "name": "Heal Flask", "potion": { "effect": "heal", "magnitude": 10 } }
    ]
  })");
  // misc/bad.json — one item missing required "name", one good.
  write_file(dir / "misc" / "bad.json", R"({
    "schema_version": 1,
    "items": [
      { "id": "clutter", "name": "Clutter" },
      { "id": "no_name" }
    ]
  })");
  // Stray .json directly under data/items/ (outside any category folder) — skipped.
  write_file(dir / "stray.json", R"({"schema_version":1,"items":[{"id":"x","name":"X"}]})");
  // Unknown category folder — skipped.
  write_file(dir / "miscellany" / "junk.json", R"({"schema_version":1,"items":[{"id":"x","name":"X"}]})");
  // Non-JSON file inside a category folder — skipped.
  write_file(dir / "weapons" / "notes.txt", "not an item");

  item::Registry reg;
  const int loaded = reg.load_all(dir);

  CHECK(loaded == 4);
  CHECK(reg.size() == 4);

  const auto *sword = reg.find("steel_sword");
  REQUIRE(sword != nullptr);
  CHECK(sword->name == "Steel Sword");
  CHECK(sword->category == item::ItemCategory::Weapon);
  REQUIRE(sword->weapon.has_value());
  CHECK(sword->weapon->damage == 12);

  const auto *flask = reg.find("heal_flask");
  REQUIRE(flask != nullptr);
  CHECK(flask->category == item::ItemCategory::Potion);
  REQUIRE(flask->potion.has_value());
  CHECK(flask->potion->effect == "heal");

  const auto *clutter = reg.find("clutter");
  REQUIRE(clutter != nullptr);
  CHECK(clutter->category == item::ItemCategory::Misc);

  CHECK(reg.find("missing") == nullptr);
  CHECK(reg.find("no_name") == nullptr); // skipped element inside a valid file
  CHECK(reg.find("x") == nullptr);
}

TEST_CASE("registry load_all: duplicate id across two files warns and keeps the first") {
  const auto dir = temp_dir("duplicate");
  write_file(dir / "weapons" / "a.json", R"({
    "schema_version": 1,
    "items": [ { "id": "sword", "name": "Sword A", "weapon": { "damage": 5 } } ]
  })");
  write_file(dir / "weapons" / "b.json", R"({
    "schema_version": 1,
    "items": [ { "id": "sword", "name": "Sword B", "weapon": { "damage": 9 } } ]
  })");

  item::Registry reg;
  const int loaded = reg.load_all(dir);

  CHECK(loaded == 1);
  const auto *sword = reg.find("sword");
  REQUIRE(sword != nullptr);
  CHECK(sword->name == "Sword A");
}
