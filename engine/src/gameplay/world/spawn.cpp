#include <corundum/gameplay/world/spawn.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/world/actors/actor.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/resources/character_registry.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace corundum::gameplay::world {

  std::expected<Scene, std::string> spawn_world(const corundum::core::GameConfig &cfg,
                                                const corundum::resources::CharacterRegistry &registry,
                                                const corundum::gameplay::world::tilemap::Tilemap &tilemap,
                                                std::optional<corundum::gameplay::component::Position> player_pos) {
    using corundum::gameplay::component::Animation;
    using corundum::gameplay::component::DialogueRef;
    using corundum::gameplay::component::Position;
    using corundum::gameplay::component::Sprite;
    using corundum::gameplay::component::Velocity;
    using corundum::gameplay::entity::World;
    using corundum::resources::AnimId;
    using corundum::resources::SpriteId;

    World world;

    const float dw = static_cast<float>(tilemap.diamond_w());
    const float dh = static_cast<float>(tilemap.diamond_h());

    const std::string map_stem = std::filesystem::path(tilemap.path).stem().string();
    const std::string actors_path = std::format("{}/{}.json", cfg.paths.spawn_points_dir, map_stem);

    auto spawn_points_result = corundum::gameplay::world::load_spawn_points(actors_path);
    if (!spawn_points_result)
      return std::unexpected(spawn_points_result.error());
    const auto &spawn_points = *spawn_points_result;

    // Spawn position precedence: explicit arg > per-map spawn_points > game.json > built-in (8,8).
    const Position spawn_pos =
        player_pos.value_or(spawn_points.player ? Position{spawn_points.player->col, spawn_points.player->row}
                                                : Position{cfg.player.col, cfg.player.row});

    const SpriteId walk_sid = registry.get_sprite_id(cfg.player.walk_sprite);
    if (walk_sid == corundum::resources::k_null_sprite_id)
      return std::unexpected(std::format("[corundum] unknown player walk sprite '{}' (game.json player.walk_sprite)",
                                         cfg.player.walk_sprite));

    const SpriteId idle_sid = registry.get_sprite_id(cfg.player.idle_sprite);
    if (idle_sid == corundum::resources::k_null_sprite_id)
      return std::unexpected(std::format("[corundum] unknown player idle sprite '{}' (game.json player.idle_sprite)",
                                         cfg.player.idle_sprite));

    std::array<uint8_t, corundum::resources::k_num_anim_ids> walk_counts{};
    std::array<uint8_t, corundum::resources::k_num_anim_ids> idle_counts{};
    corundum::gameplay::component::BoundingBox player_bb{};
    float walk_fd = 0.f;
    float idle_fd = 0.f;

    if (const auto *sd = registry.get_sprite_by_id(walk_sid)) {
      for (uint8_t i = 0; i < corundum::resources::k_num_anim_ids; ++i)
        walk_counts[i] = static_cast<uint8_t>(sd->anim_frames[i].size());
      if (const auto *sh = registry.get_sheet(sd->sheet_id)) {
        const int rfw = corundum::resources::rendered_frame_width(sd->col_span, sh->frame_width, sh->spacing_x);
        const int rfh = corundum::resources::rendered_frame_height(sd->row_span, sh->frame_height, sh->spacing_y);
        const int bb_w = sd->collision_w > 0 ? sd->collision_w : rfw;
        const int bb_h = sd->collision_h > 0 ? sd->collision_h : rfh;
        // BoundingBox in tile-grid units from sprite pixel dimensions.
        player_bb.col_span = static_cast<float>(bb_w) / dw;
        player_bb.row_span = static_cast<float>(bb_h) * sd->walk_around_offset / dh;
      }
      if (sd->fps > 0.f)
        walk_fd = 1.f / sd->fps;
    }
    if (const auto *sd = registry.get_sprite_by_id(idle_sid)) {
      for (uint8_t i = 0; i < corundum::resources::k_num_anim_ids; ++i)
        idle_counts[i] = static_cast<uint8_t>(sd->anim_frames[i].size());
      if (sd->fps > 0.f)
        idle_fd = 1.f / sd->fps;
    }

    Animation player_anim{};
    player_anim.frame_counts = idle_counts;

    auto player = spawn(world, spawn_pos, Velocity{0.f, 0.f}, Sprite{idle_sid, AnimId::Default, 0}, player_anim);
    world.collisions.insert(player, player_bb.col_span, player_bb.row_span);
    world.facings.insert(player, corundum::gameplay::component::FacingDir::South);
    if (idle_fd > 0.f)
      world.animations.frame_duration_ref(player) = idle_fd;
    world.motion_sprites.insert(player, walk_sid, idle_sid, walk_counts, idle_counts, 0.05f, 0.12f, walk_fd, idle_fd);

    if (static_cast<std::size_t>(1) + spawn_points.actors.size() > corundum::gameplay::entity::k_max_entities)
      return std::unexpected(std::format("[crpg] too many entities for '{}': {} actors + 1 player exceeds limit of {}",
                                         map_stem, spawn_points.actors.size(),
                                         corundum::gameplay::entity::k_max_entities));

    for (const auto &a : spawn_points.actors) {
      const float col = static_cast<float>(a.col);
      const float row_f = static_cast<float>(a.row);
      const SpriteId sid = registry.get_sprite_id(a.sprite_name);
      if (sid == corundum::resources::k_null_sprite_id) {
        return std::unexpected(std::format("[crpg] unknown sprite '{}'", a.sprite_name));
      }

      corundum::gameplay::component::BoundingBox bb{};
      if (const auto *sd = registry.get_sprite_by_id(sid)) {
        if (const auto *sh = registry.get_sheet(sd->sheet_id)) {
          const int rfw = corundum::resources::rendered_frame_width(sd->col_span, sh->frame_width, sh->spacing_x);
          const int rfh = corundum::resources::rendered_frame_height(sd->row_span, sh->frame_height, sh->spacing_y);
          const int bb_w = sd->collision_w > 0 ? sd->collision_w : rfw;
          const int bb_h = sd->collision_h > 0 ? sd->collision_h : rfh;
          bb.col_span = static_cast<float>(bb_w) / dw;
          bb.row_span = static_cast<float>(bb_h) * sd->walk_around_offset / dh;
        }
      }

      Animation npc_anim{};
      if (const auto *sd = registry.get_sprite_by_id(sid)) {
        for (uint8_t i = 0; i < corundum::resources::k_num_anim_ids; ++i)
          npc_anim.frame_counts[i] = static_cast<uint8_t>(sd->anim_frames[i].size());
      }

      corundum::gameplay::entity::EntityId eid;
      if (!a.dialogue_ref.empty())
        eid = spawn(world, Position{col, row_f}, Velocity{}, Sprite{sid, AnimId::Default, 0},
                    DialogueRef{a.dialogue_ref});
      else
        eid = spawn(world, Position{col, row_f}, Velocity{}, Sprite{sid, AnimId::Default, 0});
      world.animations.insert(eid);
      world.animations.set_frame_counts(eid, npc_anim.frame_counts);
      world.collisions.insert(eid, bb.col_span, bb.row_span);

      using FDir = corundum::gameplay::component::FacingDir;
      static constexpr std::array<std::pair<std::string_view, FDir>, 8> k_facing_map{{
          {"north", FDir::North},
          {"east", FDir::East},
          {"west", FDir::West},
          {"south", FDir::South},
          {"northeast", FDir::NorthEast},
          {"southeast", FDir::SouthEast},
          {"southwest", FDir::SouthWest},
          {"northwest", FDir::NorthWest},
      }};
      auto it = std::ranges::find_if(k_facing_map, [&](const auto &kv) { return kv.first == a.facing; });
      const FDir npc_facing = (it != k_facing_map.end()) ? it->second : FDir::South;
      world.facings.insert(eid, npc_facing);
    }

    Scene result;
    result.world = std::move(world);
    result.player = player;
    return result;
  }

} // namespace corundum::gameplay::world
