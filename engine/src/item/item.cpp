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

    /** @brief Item batch file JSON schema version. */
    inline constexpr int k_item_schema_version = 1;

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

    static Item parse_item_element(const json &root) {
      // Schema guarantees: id and name are present and non-empty.
      Item item;
      item.id = root["id"].get<std::string>();
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

  std::string_view category_dir_name(ItemCategory c) noexcept {
    switch (c) {
      case ItemCategory::Weapon:
        return "weapons";
      case ItemCategory::Apparel:
        return "apparel";
      case ItemCategory::Potion:
        return "potions";
      case ItemCategory::Misc:
        return "misc";
    }
    return "misc";
  }

  std::optional<ItemCategory> category_from_dir_name(std::string_view name) noexcept {
    if (name == "weapons")
      return ItemCategory::Weapon;
    if (name == "apparel")
      return ItemCategory::Apparel;
    if (name == "potions")
      return ItemCategory::Potion;
    if (name == "misc")
      return ItemCategory::Misc;
    return std::nullopt;
  }

  std::expected<std::vector<Item>, std::string> load_item_file(const std::filesystem::path &path,
                                                               ItemCategory category) {
    const auto path_string = path.string();

    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("cannot open item file: {}", path_string));

    json root;
    try {
      root = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("malformed item JSON in {}: {}", path_string, e.what()));
    }

    if (!root.contains("schema_version"))
      return std::unexpected(std::format("item file '{}' missing 'schema_version'", path_string));
    if (!root["schema_version"].is_number_integer())
      return std::unexpected(std::format("item file '{}' field 'schema_version' has wrong type", path_string));
    const int schema_version = root["schema_version"].get<int>();
    if (schema_version != k_item_schema_version)
      return std::unexpected(std::format("item file '{}' has schema_version {}, but this engine expects {}",
                                         path_string, schema_version, k_item_schema_version));

    if (!root.contains("items"))
      return std::unexpected(std::format("item file '{}' missing 'items'", path_string));
    const auto &items_arr = root["items"];
    if (!items_arr.is_array())
      return std::unexpected(std::format("item file '{}' 'items' must be an array", path_string));

    std::vector<Item> items;
    items.reserve(items_arr.size());

    for (const auto &elem : items_arr) {
      json element = elem;
      if (element.contains("category"))
        std::println(stderr,
                     "[item] '{}': explicit 'category' is overridden by the folder; use category folders "
                     "instead of writing 'category' by hand",
                     path_string);
      // The category folder is authoritative; inject it before validating.
      element["category"] = to_string(category);

      auto sv = core::item_schema().validate(element);
      if (!sv) {
        std::println(stderr, "[item] skipping invalid item in '{}': {}", path_string, sv.error());
        continue;
      }

      items.push_back(parse_item_element(element));
    }

    return items;
  }

  int Registry::load_all(const std::filesystem::path &dir) {
    int loaded = 0;

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      std::println("[item] no item directory at '{}'", dir.string());
      return loaded;
    }

    for (const auto &category_entry : std::filesystem::directory_iterator(dir)) {
      if (!category_entry.is_directory())
        continue;

      const auto category = category_from_dir_name(category_entry.path().filename().string());
      if (!category) {
        std::println(stderr, "[item] unknown category folder '{}', skipping",
                     category_entry.path().filename().string());
        continue;
      }

      for (const auto &file_entry : std::filesystem::directory_iterator(category_entry.path())) {
        if (file_entry.path().extension() != ".json")
          continue;

        auto result = load_item_file(file_entry.path(), *category);
        if (!result) {
          std::println(stderr, "[item] skipping '{}': {}", file_entry.path().filename().string(), result.error());
          continue;
        }

        for (auto &item : *result) {
          const std::string id = item.id;
          if (items_.contains(id))
            std::println(stderr, "[item] duplicate item id '{}' — '{}' is shadowed", id,
                         file_entry.path().filename().string());
          else {
            items_.emplace(id, std::move(item));
            ++loaded;
          }
        }
      }
    }

    return loaded;
  }

  const Item *Registry::find(std::string_view id) const {
    const auto it = items_.find(id);
    return it != items_.end() ? &it->second : nullptr;
  }

} // namespace corundum::item
