// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/dialogue/dialogue.hpp>

#include <nlohmann/json_fwd.hpp>

namespace corundum::gameplay::dialogue {

  /** @brief Serialize an in-memory Graph to JSON matching the engine's dialogue schema.
   *  @param graph A fully-constructed dialogue graph.
   *  @return nlohmann::json object suitable for write_json().
   */
  [[nodiscard]] nlohmann::json serialize(const Graph &graph);

} // namespace corundum::gameplay::dialogue
