#pragma once
#include <corundum/gameplay/quest/quest.hpp>

#include <nlohmann/json.hpp>

namespace corundum::gameplay::quest {

  /** @brief Serialize an in-memory Quest to JSON matching the engine's quest schema.
   *  @param quest A fully-constructed quest document.
   *  @return nlohmann::json object suitable for write_json().
   */
  [[nodiscard]] nlohmann::json serialize_quest(const Quest &quest);

} // namespace corundum::gameplay::quest
