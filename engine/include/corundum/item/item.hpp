#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace corundum::item {

  /** @brief What kind of thing an item is; drives inventory grouping. */
  enum class ItemCategory : uint8_t { Apparel, Misc, Potion, Weapon };

  /** @brief Parse a category name ("weapon", "apparel", ...). Unknown or absent → Misc. */
  [[nodiscard]] ItemCategory category_from_string(std::string_view s) noexcept;
  /** @brief Canonical serialized form of a category, matching the JSON schema enum. */
  [[nodiscard]] std::string_view to_string(ItemCategory c) noexcept;

  /** @brief On-disk directory name for a category's batch files under data/items/. */
  [[nodiscard]] std::string_view category_dir_name(ItemCategory c) noexcept;
  /** @brief Parse a category directory name ("weapons", "apparel", ...). Unknown → nullopt. */
  [[nodiscard]] std::optional<ItemCategory> category_from_dir_name(std::string_view name) noexcept;

  /** @brief Category payload for clothing/armour. */
  struct ApparelData {
    int defense = 0;
    std::string slot; // free-form for now (e.g. "head", "body") — no equip system exists
                      // yet to define a closed slot set.
  };

  /** @brief Category payload for consumables. */
  struct PotionData {
    std::string effect; // free-form identifier, e.g. "heal" — read by game code via
                        // on_event/on_fixed_update, same convention as the existing
                        // "give_item"/"reputation" EventActions.
    int magnitude = 0;
  };

  /** @brief Category payload for weapons. */
  struct WeaponData {
    int damage = 0;
  };

  /** @brief A static item definition loaded from data/items/<category>/<file>.json.
   *
   *  The category folder is authoritative: the loader injects Item::category from it.
   *  The filename inside a category folder is author-chosen (grouped however the
   *  author likes) and carries no meaning the engine parses.
   *  Runtime quantity is tracked separately as an "item.<id>" count in the FlagStore. */
  struct Item {
    std::optional<ApparelData> apparel; ///< Populated when category is Apparel.
    ItemCategory category = ItemCategory::Misc;
    std::string description;          ///< Flavor / tooltip text.
    std::string icon;                 ///< Optional icon reference (unused in MVP; reserved).
    std::string id;                   ///< Unique key.
    std::string name;                 ///< Display name.
    std::optional<PotionData> potion; ///< Populated when category is Potion.
    std::optional<WeaponData> weapon; ///< Populated when category is Weapon.
  };

} // namespace corundum::item
