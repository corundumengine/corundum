#include <corundum/core/json_schema.hpp>
#include <corundum/item/loader.hpp>
#include <corundum/item/registry.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>

using json = nlohmann::json;

namespace corundum::item {

  namespace {

    struct LoadError : std::runtime_error {
      using std::runtime_error::runtime_error;
    };

    static ItemCategory parse_category(const json &root) {
      if (root.contains("category"))
        return category_from_string(root["category"].get<std::string>());
      return ItemCategory::Misc;
    }

    static WeaponData parse_weapon(const json &j) {
      WeaponData w;
      if (j.contains("damage"))
        w.damage = j["damage"].get<int>();
      return w;
    }

    static ApparelData parse_apparel(const json &j) {
      ApparelData a;
      if (j.contains("slot"))
        a.slot = j["slot"].get<std::string>();
      if (j.contains("defense"))
        a.defense = j["defense"].get<int>();
      return a;
    }

    static PotionData parse_potion(const json &j) {
      PotionData p;
      if (j.contains("effect"))
        p.effect = j["effect"].get<std::string>();
      if (j.contains("magnitude"))
        p.magnitude = j["magnitude"].get<int>();
      return p;
    }

    static Item load_item_impl(const std::string &path) {
      std::ifstream f(path);
      if (!f)
        throw LoadError(std::format("cannot open item file: {}", path));

      const json root = [&] {
        try {
          return json::parse(f, nullptr, true, true);
        } catch (const json::exception &e) {
          throw LoadError(std::format("malformed item JSON in {}: {}", path, e.what()));
        }
      }();

      // ── Schema validation ──────────────────────────────────────────────────
      {
        auto sv = core::item_schema().validate(root);
        if (!sv)
          throw LoadError(std::format("[schema] {}: {}", path, sv.error()));
      }

      // Schema guarantees: name is present and non-empty; id is non-empty when present.
      Item item;
      if (root.contains("id") && root["id"].is_string())
        item.id = root["id"].get<std::string>();
      else
        item.id = std::filesystem::path(path).stem().string();

      item.name = root["name"].get<std::string>();

      if (root.contains("description"))
        item.description = root["description"].get<std::string>();

      if (root.contains("icon"))
        item.icon = root["icon"].get<std::string>();

      // Schema guarantees: category enum, and the payload object present when category
      // is declared. Absent category → Misc, absent payload → defaulted struct.
      item.category = parse_category(root);

      if (item.category == ItemCategory::Weapon && root.contains("weapon"))
        item.weapon = parse_weapon(root["weapon"]);
      else if (item.category == ItemCategory::Apparel && root.contains("apparel"))
        item.apparel = parse_apparel(root["apparel"]);
      else if (item.category == ItemCategory::Potion && root.contains("potion"))
        item.potion = parse_potion(root["potion"]);

      return item;
    }

  } // namespace

  ItemCategory category_from_string(std::string_view s) noexcept {
    if (s == "weapon")
      return ItemCategory::Weapon;
    if (s == "apparel")
      return ItemCategory::Apparel;
    if (s == "potion")
      return ItemCategory::Potion;
    // "misc" and anything unrecognized — schema enum checked up front.
    return ItemCategory::Misc;
  }

  std::string_view to_string(ItemCategory c) noexcept {
    switch (c) {
      case ItemCategory::Weapon:
        return "weapon";
      case ItemCategory::Apparel:
        return "apparel";
      case ItemCategory::Potion:
        return "potion";
      case ItemCategory::Misc:
        return "misc";
    }
    return "misc";
  }

  std::expected<Item, std::string> load_item(const std::filesystem::path &path) {
    try {
      return load_item_impl(path.string());
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

  int Registry::load_all(const std::filesystem::path &dir) {
    int loaded = 0;

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      std::println("[item] no item directory at '{}'", dir.string());
      return loaded;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() != ".json")
        continue;

      auto result = load_item(entry.path());
      if (!result) {
        std::println(stderr, "[item] skipping '{}': {}", entry.path().filename().string(), result.error());
        continue;
      }

      const std::string id = result->id;
      if (items_.contains(id))
        std::println(stderr, "[item] duplicate item id '{}' — '{}' is shadowed", id, entry.path().filename().string());
      else {
        items_.emplace(id, std::move(*result));
        ++loaded;
      }
    }

    return loaded;
  }

  const Item *Registry::find(std::string_view id) const {
    const auto it = items_.find(id);
    return it != items_.end() ? &it->second : nullptr;
  }

} // namespace corundum::item
