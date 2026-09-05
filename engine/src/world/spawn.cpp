#include <corundum/world/spawn.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/world/actors/actor.hpp>
#include <corundum/world/scene.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <print>
#include <string_view>
#include <utility>

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

    /// Spawn every actor in `actors`, adding (col_off,row_off) to each actor's tile coords.
    /// dw/dh are the active tilemap's diamond width/height (for bounding-box unit conversion).
    std::expected<std::vector<EntityId>, std::string> spawn_actors(World &world, const CharacterRegistry &registry,
                                                                   const std::vector<Actor> &actors, int col_off,
                                                                   int row_off, float dw, float dh) {
      std::vector<EntityId> spawned;
      spawned.reserve(actors.size());
      for (const auto &a : actors) {
        const float col = static_cast<float>(a.col + col_off);
        const float row_f = static_cast<float>(a.row + row_off);
        const SpriteId sid = registry.get_sprite_id(a.sprite_name);
        if (sid == corundum::sprites::k_null_sprite_id)
          return std::unexpected(std::format("[engine] unknown sprite '{}'", a.sprite_name));

        corundum::entities::BoundingBox bb{};
        if (const auto *sd = registry.get_sprite_by_id(sid)) {
          if (const auto *sh = registry.get_sheet(sd->sheet_id)) {
            const int rfw = corundum::sprites::rendered_frame_width(sd->col_span, sh->frame_width, sh->spacing_x);
            const int rfh = corundum::sprites::rendered_frame_height(sd->row_span, sh->frame_height, sh->spacing_y);
            const int bb_w = sd->collision_w > 0 ? sd->collision_w : rfw;
            const int bb_h = sd->collision_h > 0 ? sd->collision_h : rfh;
            bb.col_span = static_cast<float>(bb_w) / dw;
            bb.row_span = static_cast<float>(bb_h) * sd->walk_around_offset / dh;
          }
        }

        Animation npc_anim{};
        if (const auto *sd = registry.get_sprite_by_id(sid)) {
          for (uint8_t i = 0; i < corundum::sprites::k_num_anim_ids; ++i)
            npc_anim.frame_counts[i] = static_cast<uint8_t>(sd->anim_frames[i].size());
        }

        EntityId eid;
        if (!a.dialogue_ref.empty())
          eid = spawn(world, Position{col, row_f}, Velocity{}, Sprite{sid, AnimId::Default, 0},
                      DialogueRef{a.dialogue_ref});
        else
          eid = spawn(world, Position{col, row_f}, Velocity{}, Sprite{sid, AnimId::Default, 0});
        world.animations.insert(eid);
        world.animations.set_frame_counts(eid, npc_anim.frame_counts);
        world.collisions.insert(eid, bb.col_span, bb.row_span);

        using FDir = corundum::entities::FacingDir;
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
        world.facings.insert(eid, (it != k_facing_map.end()) ? it->second : FDir::South);

        spawned.push_back(eid);
      }
      return spawned;
    }

    /// load_spawn_points(path).actors -> spawn_actors(...). Missing file -> empty vector.
    std::expected<std::vector<EntityId>, std::string>
    spawn_actors_from_file(World &world, const CharacterRegistry &registry, const std::filesystem::path &path,
                           int col_off, int row_off, float dw, float dh) {
      auto sp = load_spawn_points(path);
      if (!sp)
        return std::unexpected(sp.error());
      return spawn_actors(world, registry, sp->actors, col_off, row_off, dw, dh);
    }

  } // namespace

  std::expected<Scene, std::string> spawn_world(const corundum::core::GameConfig &cfg,
                                                const corundum::sprites::CharacterRegistry &registry,
                                                const corundum::world::tilemap::Tilemap &tilemap,
                                                std::optional<corundum::entities::Position> player_pos,
                                                bool spawn_file_actors) {
    using corundum::entities::Animation;
    using corundum::entities::MotionSpriteTable;
    using corundum::entities::Position;
    using corundum::entities::Sprite;
    using corundum::entities::Velocity;
    using corundum::entities::World;
    using corundum::sprites::AnimId;
    using corundum::sprites::SpriteId;

    World world;

    const float dw = static_cast<float>(tilemap.diamond_w());
    const float dh = static_cast<float>(tilemap.diamond_h());

    const std::string map_stem = std::filesystem::path(tilemap.path).stem().string();
    const auto actors_path = std::filesystem::path(cfg.paths.spawn_points_dir) / (map_stem + ".json");

    auto spawn_points_result = corundum::world::load_spawn_points(actors_path);
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
    const Position spawn_pos =
        player_pos.value_or(spawn_points.player ? Position{spawn_points.player->col, spawn_points.player->row}
                                                : Position{cfg.player.col, cfg.player.row});

    const SpriteId walk_sid = registry.get_sprite_id(cfg.player.walk_sprite);
    if (walk_sid == corundum::sprites::k_null_sprite_id)
      return std::unexpected(std::format("[engine] unknown player walk sprite '{}' (game.json player.walk_sprite)",
                                         cfg.player.walk_sprite));

    const SpriteId idle_sid = registry.get_sprite_id(cfg.player.idle_sprite);
    if (idle_sid == corundum::sprites::k_null_sprite_id)
      return std::unexpected(std::format("[engine] unknown player idle sprite '{}' (game.json player.idle_sprite)",
                                         cfg.player.idle_sprite));

    std::array<uint8_t, corundum::sprites::k_num_anim_ids> walk_counts{};
    std::array<uint8_t, corundum::sprites::k_num_anim_ids> idle_counts{};
    corundum::entities::BoundingBox player_bb{};
    float walk_fd = 0.f;
    float idle_fd = 0.f;

    if (const auto *sd = registry.get_sprite_by_id(walk_sid)) {
      for (uint8_t i = 0; i < corundum::sprites::k_num_anim_ids; ++i)
        walk_counts[i] = static_cast<uint8_t>(sd->anim_frames[i].size());
      if (const auto *sh = registry.get_sheet(sd->sheet_id)) {
        const int rfw = corundum::sprites::rendered_frame_width(sd->col_span, sh->frame_width, sh->spacing_x);
        const int rfh = corundum::sprites::rendered_frame_height(sd->row_span, sh->frame_height, sh->spacing_y);
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
      for (uint8_t i = 0; i < corundum::sprites::k_num_anim_ids; ++i)
        idle_counts[i] = static_cast<uint8_t>(sd->anim_frames[i].size());
      if (sd->fps > 0.f)
        idle_fd = 1.f / sd->fps;
    }

    Animation player_anim{};
    player_anim.frame_counts = idle_counts;

    auto player = spawn(world, spawn_pos, Velocity{0.f, 0.f}, Sprite{idle_sid, AnimId::Default, 0}, player_anim);
    world.collisions.insert(player, player_bb.col_span, player_bb.row_span);
    world.facings.insert(player, corundum::entities::FacingDir::South);
    if (idle_fd > 0.f)
      world.animations.frame_duration_ref(player) = idle_fd;
    world.motion_sprites.insert(player, MotionSpriteTable::Config{
                                            .walk_id = walk_sid,
                                            .idle_id = idle_sid,
                                            .walk_counts = walk_counts,
                                            .idle_counts = idle_counts,
                                            .idle_to_walk_delay = 0.05f,
                                            .walk_to_idle_delay = 0.12f,
                                            .walk_frame_duration = walk_fd,
                                            .idle_frame_duration = idle_fd,
                                        });

    if (static_cast<std::size_t>(1) + spawn_points.actors.size() > corundum::entities::k_max_entities)
      return std::unexpected(
          std::format("[engine] too many entities for '{}': {} actors + 1 player exceeds limit of {}", map_stem,
                      spawn_points.actors.size(), corundum::entities::k_max_entities));

    if (spawn_file_actors) {
      auto spawned = spawn_actors(world, registry, spawn_points.actors, 0, 0, dw, dh);
      if (!spawned)
        return std::unexpected(spawned.error());
    }

    Scene result;
    result.world = std::move(world);
    result.player = player;
    return result;
  }

  void sync_chunk_actors(Scene &scene, const corundum::render::RenderState &render,
                         const corundum::core::GameConfig &cfg, const corundum::sprites::CharacterRegistry &registry) {
    namespace tm = corundum::world::tilemap;

    if (render.mode != corundum::render::RenderMode::World)
      return;
    if (render.chunks.active_empty())
      return;
    if (scene.mode != GameMode::Exploring)
      return; // don't churn actors mid-dialogue / mid-prompt

    const auto &ref_tm = render.chunks.active_at(0).tilemap;
    const float dw = static_cast<float>(ref_tm.diamond_w());
    const float dh = static_cast<float>(ref_tm.diamond_h());
    const int cs = render.manifest.chunk_size;

    const auto is_resident = [&](tm::ChunkCoord c) {
      for (const auto &e : render.chunks.active())
        if (e.coord == c)
          return true;
      return false;
    };

    // 1. Despawn actors whose chunk is no longer resident.
    std::erase_if(scene.chunk_actors, [&](const ChunkActorSet &set) {
      if (is_resident(set.coord))
        return false;
      for (const EntityId eid : set.entities)
        if (scene.world.entities.is_live(eid))
          corundum::entities::mark_for_deletion(scene.world, eid);
      return true;
    });

    // 2. Spawn actors for newly-resident chunks.
    const auto is_tracked = [&](tm::ChunkCoord c) {
      for (const auto &set : scene.chunk_actors)
        if (set.coord == c)
          return true;
      return false;
    };
    for (const auto &entry : render.chunks.active()) {
      if (is_tracked(entry.coord))
        continue;
      const auto path = std::filesystem::path(cfg.paths.spawn_points_dir) /
                        std::format("chunk_{}_{}.json", entry.coord.col, entry.coord.row);
      auto spawned =
          spawn_actors_from_file(scene.world, registry, path, entry.coord.col * cs, entry.coord.row * cs, dw, dh);
      if (!spawned) {
        std::println(stderr, "[engine] WARN: chunk ({}, {}) actors skipped: {}", entry.coord.col, entry.coord.row,
                     spawned.error());
        scene.chunk_actors.push_back(ChunkActorSet{entry.coord, {}}); // track anyway — don't retry every frame
        continue;
      }
      scene.chunk_actors.push_back(ChunkActorSet{entry.coord, std::move(*spawned)});
    }
  }

} // namespace corundum::world
