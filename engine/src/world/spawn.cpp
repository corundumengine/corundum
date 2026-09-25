// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/spawn.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <corundum/core/direction.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/world/actors/actor.hpp>
#include <corundum/world/scene.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace corundum::world {

  namespace {

    using corundum::entities::Animation;
    using corundum::entities::DialogueRef;
    using corundum::entities::EntityId;
    using corundum::entities::Position;
    using corundum::entities::Sprite;
    using corundum::entities::Velocity;
    using corundum::entities::World;
    using corundum::sprites::AnimId;
    using corundum::sprites::CharacterRegistry;
    using corundum::sprites::SpriteId;

    /// Spawn-relevant stats read from one registry sprite: frame counts, collision footprint, frame duration.
    struct SpriteStats {
      std::array<uint8_t, corundum::sprites::k_num_anim_ids> frame_counts{};

      float col_span{};
      float row_span{};
      float frame_duration{};
    };

    /// Read @p sid's stats, leaving every field at its default when the id is unknown.
    SpriteStats read_sprite_stats(const CharacterRegistry &registry, SpriteId sid) {
      SpriteStats stats;
      const auto *sprite = registry.get_sprite_by_id(sid);
      if (sprite == nullptr)
        return stats;

      for (uint8_t i = 0; i < corundum::sprites::k_num_anim_ids; ++i)
        stats.frame_counts[i] = static_cast<uint8_t>(sprite->anim_frames[i].size());
      stats.col_span = sprite->footprint_col_span;
      stats.row_span = sprite->footprint_row_span;
      if (sprite->fps > 0.f)
        stats.frame_duration = 1.f / sprite->fps;
      return stats;
    }

    /// Destroy every entity in @p spawned so a failed batch never leaves live, untracked actors.
    void rollback_spawned(World &world, const std::vector<EntityId> &spawned) {
      for (const EntityId eid : spawned)
        corundum::entities::despawn(world, eid);
    }

    /// Spawn every actor in @p actors, adding (col_off, row_off) to each actor's tile coords.
    std::expected<std::vector<EntityId>, std::string> spawn_actors(World &world, const CharacterRegistry &registry,
                                                                   const std::vector<Actor> &actors, int col_off,
                                                                   int row_off) {
      std::vector<EntityId> spawned;
      spawned.reserve(actors.size());
      const auto fail = [&](std::string message) -> std::expected<std::vector<EntityId>, std::string> {
        rollback_spawned(world, spawned);
        return std::unexpected(std::move(message));
      };

      for (const Actor &actor : actors) {
        const Position position{
            .col = static_cast<float>(actor.col + col_off),
            .row = static_cast<float>(actor.row + row_off),
        };
        const SpriteId sprite_id = registry.get_sprite_id(actor.sprite_name);
        if (sprite_id == corundum::sprites::k_null_sprite_id)
          return fail(std::format("[engine] unknown sprite '{}'", actor.sprite_name));

        const SpriteStats stats = read_sprite_stats(registry, sprite_id);
        const Sprite sprite{.sprite_id = sprite_id, .anim_id = AnimId::Default, .frame_index = 0};
        const Velocity velocity{};

        const std::optional<EntityId> spawned_id =
            actor.dialogue_ref.empty()
                ? spawn(world, position, velocity, sprite)
                : spawn(world, position, velocity, sprite, DialogueRef{.graph_id = actor.dialogue_ref});
        if (!spawned_id)
          return fail(std::format("[engine] entity pool exhausted spawning actors (limit {})",
                                  corundum::entities::k_max_entities));
        const EntityId eid = *spawned_id;
        world.animations.insert(eid, stats.frame_duration);
        world.animations.set_frame_counts(eid, stats.frame_counts);
        world.collisions.insert(eid, stats.col_span, stats.row_span);

        if (!actor.id.empty())
          world.actor_ids.insert(eid, actor.id);

        const std::optional<corundum::core::Direction> facing = corundum::core::direction_from_name(actor.facing);
        world.facings.insert(eid, facing.value_or(corundum::core::Direction::South));

        spawned.push_back(eid);
      }
      return spawned;
    }

    /// Load @p path and spawn its actors offset by (col_off, row_off); a missing file yields no actors.
    std::expected<std::vector<EntityId>, std::string> spawn_actors_from_file(World &world,
                                                                             const CharacterRegistry &registry,
                                                                             const std::filesystem::path &path,
                                                                             int col_off, int row_off) {
      auto spawn_points = load_spawn_points(path);
      if (!spawn_points)
        return std::unexpected(spawn_points.error());
      return spawn_actors(world, registry, spawn_points->actors, col_off, row_off);
    }

    /// Spawn the player (collision, facing, animation, motion sprite) from @p cfg's player sprite names.
    std::expected<EntityId, std::string> spawn_player(World &world, const CharacterRegistry &registry,
                                                      const corundum::core::GameConfig &cfg, Position spawn_pos) {
      const SpriteId walk_id = registry.get_sprite_id(cfg.player.walk_sprite);
      if (walk_id == corundum::sprites::k_null_sprite_id)
        return std::unexpected(std::format("[engine] unknown player walk sprite '{}' (game.json player.walk_sprite)",
                                           cfg.player.walk_sprite));

      const SpriteId idle_id = registry.get_sprite_id(cfg.player.idle_sprite);
      if (idle_id == corundum::sprites::k_null_sprite_id)
        return std::unexpected(std::format("[engine] unknown player idle sprite '{}' (game.json player.idle_sprite)",
                                           cfg.player.idle_sprite));

      const SpriteStats walk = read_sprite_stats(registry, walk_id);
      const SpriteStats idle = read_sprite_stats(registry, idle_id);

      Animation player_anim{};
      player_anim.frame_counts = idle.frame_counts;

      const Sprite sprite{.sprite_id = idle_id, .anim_id = AnimId::Default, .frame_index = 0};
      const Velocity velocity{};
      const std::optional<EntityId> spawned = spawn(world, spawn_pos, velocity, sprite, player_anim);
      if (!spawned)
        return std::unexpected(std::format("[engine] entity pool exhausted spawning the player (limit {})",
                                           corundum::entities::k_max_entities));
      const EntityId player = *spawned;
      world.collisions.insert(player, walk.col_span, walk.row_span);
      world.facings.insert(player, corundum::core::Direction::South);
      if (idle.frame_duration > 0.f)
        world.animations.frame_duration_ref(player) = idle.frame_duration;
      world.motion_sprites.insert(player, corundum::entities::MotionSpriteTable::Config{
                                              .walk_id = walk_id,
                                              .idle_id = idle_id,
                                              .walk_counts = walk.frame_counts,
                                              .idle_counts = idle.frame_counts,
                                              .idle_to_walk_delay = 0.05f,
                                              .walk_to_idle_delay = 0.12f,
                                              .walk_frame_duration = walk.frame_duration,
                                              .idle_frame_duration = idle.frame_duration,
                                          });
      return player;
    }

    /// Queue every live entity in @p entities for deletion; already-queued entities are ignored.
    void mark_entities_for_deletion(World &world, const std::vector<EntityId> &entities) {
      for (const EntityId eid : entities)
        if (world.entities.is_live(eid))
          corundum::entities::mark_for_deletion(world, eid);
    }

    /// True if any entity in @p entities is still live, including ones only queued for deletion.
    bool any_live(const World &world, const std::vector<EntityId> &entities) {
      return std::ranges::any_of(entities, [&](EntityId eid) { return world.entities.is_live(eid); });
    }

    /// Queue the actors of every chunk that has left the active window, and flag its set departed.
    void despawn_departed_chunks(Scene &scene, const corundum::render::RenderState &render) {
      for (ChunkActorSet &set : scene.chunk_actors) {
        if (render.chunks.is_active(set.coord))
          continue;
        mark_entities_for_deletion(scene.world, set.entities);
        set.departed = true;
      }
    }

    /// Spawn actors for active chunks with no tracked set, and respawn a departed chunk that has returned once its
    /// previous actors are flushed. A resident, non-departed set is never re-read: it already spawned (or failed) for
    /// this residency.
    void spawn_new_chunks(Scene &scene, const corundum::render::RenderState &render,
                          const corundum::core::GameConfig &cfg, const CharacterRegistry &registry, int chunk_size) {
      for (const auto &entry : render.chunks.active()) {
        const auto set = std::ranges::find(scene.chunk_actors, entry.coord, &ChunkActorSet::coord);
        if (set != scene.chunk_actors.end() && (!set->departed || any_live(scene.world, set->entities)))
          continue;

        const auto path = std::filesystem::path(cfg.paths.spawn_points_dir) /
                          std::format("chunk_{}_{}.json", entry.coord.col, entry.coord.row);
        auto spawned = spawn_actors_from_file(scene.world, registry, path, entry.coord.col * chunk_size,
                                              entry.coord.row * chunk_size);
        if (!spawned) {
          std::println(stderr, "[engine] WARN: chunk ({}, {}) actors skipped: {}", entry.coord.col, entry.coord.row,
                       spawned.error());
          if (set != scene.chunk_actors.end()) {
            set->entities.clear();
            set->load_failed = true;
            set->departed = false;
          } else {
            scene.chunk_actors.push_back(ChunkActorSet{.coord = entry.coord, .load_failed = true});
          }
          continue;
        }

        if (set != scene.chunk_actors.end()) {
          set->entities = std::move(*spawned);
          set->load_failed = false;
          set->departed = false;
        } else {
          scene.chunk_actors.push_back(ChunkActorSet{.coord = entry.coord, .entities = std::move(*spawned)});
        }
      }
    }

    /// Drop tracked sets once they are out of the window and all their actors have been despawned.
    void prune_departed_chunks(Scene &scene, const corundum::render::RenderState &render) {
      std::erase_if(scene.chunk_actors, [&](const ChunkActorSet &set) {
        return !render.chunks.is_active(set.coord) && !any_live(scene.world, set.entities);
      });
    }

  } // namespace

  std::expected<std::unique_ptr<Scene>, std::string> spawn_world(const corundum::core::GameConfig &cfg,
                                                                 const corundum::sprites::CharacterRegistry &registry,
                                                                 const corundum::world::tilemap::Tilemap &tilemap,
                                                                 std::optional<corundum::entities::Position> player_pos,
                                                                 bool spawn_file_actors) {
    auto scene = std::make_unique<Scene>();
    World &world = scene->world;

    const std::string map_stem = std::filesystem::path(tilemap.path).stem().string();
    const auto actors_path = std::filesystem::path(cfg.paths.spawn_points_dir) / (map_stem + ".json");

    auto spawn_points_result = load_spawn_points(actors_path);
    if (!spawn_points_result)
      return std::unexpected(spawn_points_result.error());
    const auto &spawn_points = *spawn_points_result;

    if (spawn_file_actors) {
      if (!std::filesystem::exists(actors_path))
        std::println("[engine] 0 actors (no spawn points file at '{}')", actors_path.string());
      else
        std::println("[engine] Loaded {} actors from '{}'", spawn_points.actors.size(), actors_path.string());
    }

    // Spawn position precedence: explicit arg > per-map spawn_points > game.json > built-in (8,8).
    const Position spawn_pos = player_pos.value_or(
        spawn_points.player ? Position{.col = spawn_points.player->col, .row = spawn_points.player->row}
                            : Position{.col = cfg.player.col, .row = cfg.player.row});

    auto player = spawn_player(world, registry, cfg, spawn_pos);
    if (!player)
      return std::unexpected(player.error());

    if (spawn_file_actors) {
      if (static_cast<std::size_t>(1) + spawn_points.actors.size() > corundum::entities::k_max_entities)
        return std::unexpected(
            std::format("[engine] too many entities for '{}': {} actors + 1 player exceeds limit of {}", map_stem,
                        spawn_points.actors.size(), corundum::entities::k_max_entities));

      auto spawned = spawn_actors(world, registry, spawn_points.actors, 0, 0);
      if (!spawned)
        return std::unexpected(spawned.error());
    }

    scene->zone_id = map_stem;
    scene->player = *player;
    return scene;
  }

  void sync_chunk_actors(Scene &scene, const corundum::render::RenderState &render,
                         const corundum::core::GameConfig &cfg, const corundum::sprites::CharacterRegistry &registry) {
    if (render.mode != corundum::render::RenderMode::World)
      return;
    if (render.chunks.active_empty())
      return;
    if (scene.mode != GameMode::Exploring)
      return; // don't churn actors mid-dialogue / mid-prompt

    const int chunk_size = render.manifest.chunk_size;

    despawn_departed_chunks(scene, render);
    spawn_new_chunks(scene, render, cfg, registry, chunk_size);
    prune_departed_chunks(scene, render);
  }

} // namespace corundum::world
