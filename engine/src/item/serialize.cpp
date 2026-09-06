#include <corundum/item/serialize.hpp>

namespace corundum::item {

  namespace {

    /** @brief Emit the common element fields (category is folder-derived, omitted). */
    [[nodiscard]] nlohmann::json serialize_item_element(const Item &item) {
      nlohmann::json j;
      j["id"] = item.id;
      j["name"] = item.name;
      if (!item.description.empty())
        j["description"] = item.description;
      if (!item.icon.empty())
        j["icon"] = item.icon;
      return j;
    }

  } // namespace

  nlohmann::json serialize(std::span<const Item> items, ItemCategory category) {
    nlohmann::json j;
    j["schema_version"] = k_item_schema_version;
    j["items"] = nlohmann::json::array();

    for (const Item &item : items) {
      nlohmann::json element = serialize_item_element(item);
      switch (category) {
        case ItemCategory::Weapon: {
          nlohmann::json payload = nlohmann::json::object();
          if (item.weapon.has_value() && item.weapon->damage != 0)
            payload["damage"] = item.weapon->damage;
          element["weapon"] = std::move(payload);
          break;
        }
        case ItemCategory::Apparel: {
          nlohmann::json payload = nlohmann::json::object();
          if (item.apparel.has_value() && !item.apparel->slot.empty())
            payload["slot"] = item.apparel->slot;
          if (item.apparel.has_value() && item.apparel->defense != 0)
            payload["defense"] = item.apparel->defense;
          element["apparel"] = std::move(payload);
          break;
        }
        case ItemCategory::Potion: {
          nlohmann::json payload = nlohmann::json::object();
          if (item.potion.has_value() && !item.potion->effect.empty())
            payload["effect"] = item.potion->effect;
          if (item.potion.has_value() && item.potion->magnitude != 0)
            payload["magnitude"] = item.potion->magnitude;
          element["potion"] = std::move(payload);
          break;
        }
        case ItemCategory::Misc:
          break;
      }
      j["items"].push_back(std::move(element));
    }

    return j;
  }

} // namespace corundum::item