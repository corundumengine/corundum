#pragma once
#include <string>

namespace corundum::gameplay::item {

  /** @brief A static item definition loaded from data/items/<id>.json.
   *  Runtime quantity is tracked separately as an "item.<id>" count in the FlagStore. */
  struct Item {
    std::string description; ///< Flavor / tooltip text.
    std::string icon;        ///< Optional icon reference (unused in MVP; reserved).
    std::string id;          ///< Unique key; matches the filename stem.
    std::string name;        ///< Display name.
  };

} // namespace corundum::gameplay::item
