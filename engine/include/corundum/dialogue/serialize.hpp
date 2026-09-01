#pragma once
#include <corundum/dialogue/dialogue.hpp>

#include <nlohmann/json.hpp>

namespace corundum::dialogue {

  /** @brief Serialize an in-memory Graph to JSON matching the engine's dialogue schema.
   *  @param graph A fully-constructed dialogue graph.
   *  @return nlohmann::json object suitable for write_json().
   */
  [[nodiscard]] nlohmann::json serialize_graph(const Graph &graph);

} // namespace corundum::dialogue
