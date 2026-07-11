#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/resources/character_registry.hpp>

#include <expected>
#include <optional>
#include <string>

namespace corundum::gameplay::world {

  /**
   * @brief Spawn the player and all actors into a fresh Scene.
   *
   * @param cfg        Game configuration (tile scale, sprite scale).
   * @param registry   Character registry for sprite ID lookups.
   * @param tilemap    Loaded tilemap (used for tile dimensions and map stem).
   * @param player_pos Tile-grid position for the player (col, row). When absent the player
   *                  is placed at tile (8, 8) of the new map.
   * @return Scene containing the populated world and player EntityId,
   *         or std::unexpected with an error description on failure.
   */
  [[nodiscard]] std::expected<Scene, std::string>
  spawn_world(const corundum::core::GameConfig &cfg, const corundum::resources::CharacterRegistry &registry,
              const corundum::gameplay::world::tilemap::Tilemap &tilemap,
              std::optional<corundum::gameplay::component::Position> player_pos = std::nullopt);

} // namespace corundum::gameplay::world
