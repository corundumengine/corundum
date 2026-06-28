#include <corundum/render/sys/render_sys.hpp>

#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/component/transform_table.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/resources/character_registry.hpp>
#include <corundum/resources/sprite.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <ranges>
#include <span>
#include <unordered_map>

using corundum::core::math::IntRect;
using corundum::resources::AnimId;
using corundum::resources::k_null_sprite_id;
using corundum::resources::k_num_anim_ids;
using corundum::resources::rendered_frame_height;
using corundum::resources::rendered_frame_width;
using corundum::resources::SpriteId;

// ── SpriteFrameIndex::get (data layer method) ────────────────────────────────

namespace corundum::render::data {

  std::optional<SpriteFrameIndex::Entry> SpriteFrameIndex::get(SpriteId sprite_id, AnimId anim_id,
                                                               uint8_t frame_index) const noexcept {
    if (sprite_id == k_null_sprite_id)
      return std::nullopt;

    const auto aid = static_cast<uint8_t>(anim_id);
    if (aid >= k_num_anim_ids)
      return std::nullopt;

    const auto sid = static_cast<std::size_t>(sprite_id);
    if (sid >= tex_by_sprite_id.size() || !tex_by_sprite_id[sid].has_value())
      return std::nullopt;

    const std::size_t slot = sid * k_num_anim_ids + aid;
    if (slot >= anim_frame_counts.size() || frame_index >= anim_frame_counts[slot])
      return std::nullopt;

    const float woff = sid < walk_offsets.size() ? walk_offsets[sid] : 0.f;
    return Entry{*tex_by_sprite_id[sid], frame_rects[anim_offsets[slot] + frame_index], woff};
  }

} // namespace corundum::render::data

// ── render::sys free functions ────────────────────────────────────────────────

namespace corundum::render::sys {

  // ── SystemManager equivalents ────────────────────────────────────────────────

  std::expected<void, std::string> initialize(data::RenderState &state) {
    state.draw_list.reserve(corundum::gameplay::entity::k_max_entities);
    return {};
  }

  void clean_up(data::RenderState & /*state*/) noexcept {}

  void update(data::RenderState & /*state*/, float /*dt*/) noexcept {}

  // ── Internal helpers (forward decls) ─────────────────────────────────────────

  static std::optional<data::ChunkEntry> load_chunk_entry(corundum::platform::Renderer &r, data::RenderState &state,
                                                          corundum::gameplay::world::tilemap::ChunkCoord c);

  static void rebuild_collision(data::RenderState &state, const corundum::core::GameConfig &cfg);

  static void sync_active_chunks(corundum::platform::Renderer &r, data::RenderState &state,
                                 const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene);

  static void render_tilemap(corundum::platform::Renderer &r, const data::RenderState &state, int z_index,
                             const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene);

  static void render_chunk(corundum::platform::Renderer &r, const data::RenderState &state,
                           const data::ChunkEntry &chunk, int z_index, const corundum::core::GameConfig &cfg,
                           const corundum::gameplay::world::Scene &scene);

  static void render_entities(corundum::platform::Renderer &r, data::RenderState &state,
                              const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                              float alpha);

  // ── load_sprite_index ────────────────────────────────────────────────────────

  void load_sprite_index(corundum::platform::Renderer &r, data::RenderState &state,
                         const corundum::resources::CharacterRegistry &registry) {
    SpriteId max_id = 0;
    for (const auto &[name, frm] : registry.frames())
      if (frm.sprite_id > max_id)
        max_id = frm.sprite_id;

    const std::size_t num_sprites = static_cast<std::size_t>(max_id) + 1;
    state.sprite_index.tex_by_sprite_id.assign(num_sprites, std::nullopt);
    const std::size_t num_slots = num_sprites * k_num_anim_ids;
    state.sprite_index.anim_offsets.assign(num_slots, 0);
    state.sprite_index.anim_frame_counts.assign(num_slots, 0);
    state.sprite_index.frame_rects.clear();

    std::unordered_map<corundum::resources::Id, uint32_t> sheet_tex;
    for (const auto &[sheet_id, sheet] : registry.sheets()) {
      auto result = r.load_texture(sheet.path);
      if (result.has_value()) {
        sheet_tex[sheet_id] = result.value();
      } else {
        std::println(stderr, "[renderer] WARNING: could not load texture '{}'", sheet.path);
        sheet_tex[sheet_id] = 0;
      }
    }

    for (const auto &[name, frm] : registry.frames()) {
      if (frm.sprite_id == k_null_sprite_id)
        continue;
      const corundum::resources::SpriteSheet *sheet = registry.get_sheet(frm.sheet_id);
      if (!sheet)
        continue;
      const auto tex_it = sheet_tex.find(frm.sheet_id);
      if (tex_it == sheet_tex.end())
        continue;

      state.sprite_index.tex_by_sprite_id[frm.sprite_id] = tex_it->second;
      const auto sid = static_cast<std::size_t>(frm.sprite_id);
      if (sid >= state.sprite_index.walk_offsets.size())
        state.sprite_index.walk_offsets.resize(sid + 1, 0.f);
      state.sprite_index.walk_offsets[sid] = frm.walk_around_offset;

      const int fw = rendered_frame_width(frm.col_span, sheet->frame_width, sheet->spacing_x);
      const int fh = rendered_frame_height(frm.row_span, sheet->frame_height, sheet->spacing_y);

      for (uint8_t a = 0; a < k_num_anim_ids; ++a) {
        const auto &coords = frm.anim_frames[a];
        const std::size_t slot = static_cast<std::size_t>(frm.sprite_id) * k_num_anim_ids + a;
        state.sprite_index.anim_offsets[slot] = static_cast<uint32_t>(state.sprite_index.frame_rects.size());
        state.sprite_index.anim_frame_counts[slot] = static_cast<uint8_t>(coords.size());
        for (const auto &c : coords) {
          const int x = sheet->offset_x + c.col * (sheet->frame_width + sheet->spacing_x);
          const int y = sheet->offset_y + c.row * (sheet->frame_height + sheet->spacing_y);
          state.sprite_index.frame_rects.push_back(IntRect{x, y, fw, fh});
        }
      }
    }
  }

  // ── load_font ────────────────────────────────────────────────────────────────

  std::expected<uint32_t, std::string> load_font(corundum::platform::Renderer &r, data::RenderState &state,
                                                 const std::string &path) {
    auto result = r.load_font(path);
    if (result.has_value())
      state.font_id = result.value();
    return result;
  }

  // ── load_ui_assets ───────────────────────────────────────────────────────────

  std::expected<void, std::string> load_ui_assets(corundum::platform::Renderer &r, data::RenderState &state) {
    constexpr std::string_view k_path = "data/sprite_sheets/ui/borders.json";
    std::ifstream f{std::string{k_path}};
    if (!f.is_open()) {
      std::println(stderr, "[renderer] WARNING: could not open '{}'", k_path);
      return {};
    }

    const nlohmann::json j = nlohmann::json::parse(f);
    const int fw = j.at("frame_width").get<int>();
    const int fh = j.at("frame_height").get<int>();
    const std::string tex_path = j.at("path").get<std::string>();

    auto make_rect = [&](std::string_view name) -> IntRect {
      const auto &fr = j.at("frames").at(std::string{name});
      const int col = fr.at("col").get<int>();
      const int row = fr.at("row").get<int>();
      const int cs = fr.at("col_span").get<int>();
      const int rs = fr.at("row_span").get<int>();
      return IntRect{col * fw, row * fh, cs * fw, rs * fh};
    };

    auto result = r.load_texture(tex_path);
    if (result.has_value()) {
      state.dialog_box.border.texture_id = result.value();
    } else {
      std::println(stderr, "[renderer] WARNING: could not load texture '{}'", tex_path);
      state.dialog_box.border.texture_id = 0;
    }

    const IntRect ul = make_rect("upper_left");
    state.dialog_box.border.tile_w = ul.width;
    state.dialog_box.border.tile_h = ul.height;

    return {};
  }

  // ── load_map ─────────────────────────────────────────────────────────────────

  std::expected<void, std::string> load_map(corundum::platform::Renderer &r, data::RenderState &state,
                                            const std::string &tilemap_path, const corundum::core::GameConfig &cfg) {
    state.mode = data::RenderMode::SingleMap;
    state.active_chunks.clear();
    state.manifest = {};
    state.agg_collisions = {};
    state.agg_triangles = {};
    state.above_z_cache.clear();

    auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(tilemap_path);
    if (!tm_result)
      return std::unexpected(tm_result.error());
    auto tilemap = std::move(*tm_result);

    std::vector<uint32_t> tex_ids;
    tex_ids.reserve(tilemap.tilesets.size());
    for (const auto &ts : tilemap.tilesets) {
      auto result = r.load_texture(ts.info.path);
      if (result.has_value()) {
        tex_ids.push_back(result.value());
      } else {
        std::println(stderr, "[renderer] WARNING: could not load texture '{}'", ts.info.path);
        tex_ids.push_back(0);
      }
    }

    std::vector<int> above_z;
    for (const auto &layer : tilemap.layers)
      if (layer.z_index > 0)
        above_z.push_back(layer.z_index);
    std::ranges::sort(above_z);
    above_z.erase(std::ranges::unique(above_z).begin(), above_z.end());

    const float tw = static_cast<float>(tilemap.tilesets[0].info.tile_width) * cfg.tile_scale;
    const float th = static_cast<float>(tilemap.tilesets[0].info.tile_height) * cfg.tile_scale;
    const std::string stem = std::filesystem::path(tilemap_path).stem().string();
    const auto portals_file = std::format("{}/{}.json", cfg.paths.portals_dir, stem);
    auto portals = corundum::gameplay::world::load_portals(portals_file, tw, th);
    if (!portals)
      return std::unexpected(portals.error());

    state.map_data = {std::move(tilemap), std::move(tex_ids), std::move(above_z), std::move(*portals)};
    return {};
  }

  // ── load_world ───────────────────────────────────────────────────────────────

  std::expected<WorldLoadInfo, std::string> load_world(corundum::platform::Renderer &r, data::RenderState &state,
                                                       const corundum::core::GameConfig &cfg) {
    using namespace corundum::gameplay::world::tilemap;

    state.mode = data::RenderMode::World;
    state.map_data = {};
    state.above_z_cache.clear();

    {
      auto manifest_result = load_world_manifest(cfg.paths.world_manifest_path);
      if (!manifest_result)
        return std::unexpected(manifest_result.error());
      state.manifest = std::move(*manifest_result);
    }
    std::println("[keystone] World manifest: {}×{} chunks of {}×{} tiles", state.manifest.chunks_wide,
                 state.manifest.chunks_tall, state.manifest.chunk_size, state.manifest.chunk_size);

    const ChunkCoord center{state.manifest.chunks_wide / 2, state.manifest.chunks_tall / 2};
    state.last_center_chunk = center;
    for (const ChunkCoord c : active_chunk_coords(center, 1, state.manifest)) {
      if (auto entry = load_chunk_entry(r, state, c))
        state.active_chunks.push_back(std::move(*entry));
    }
    rebuild_collision(state, cfg);

    const int diamond_w = state.active_chunks[0].tilemap.diamond_w();
    const int diamond_h = state.active_chunks[0].tilemap.diamond_h();
    const float half_tw = static_cast<float>(diamond_w) * cfg.tile_scale * 0.5f;
    const float half_th = static_cast<float>(diamond_h) * cfg.tile_scale * 0.5f;
    const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                      : state.manifest.chunks_tall * state.manifest.chunk_size;
    const float x_origin = static_cast<float>(total_h - 1) * half_tw;

    const int center_col = state.manifest.chunks_wide * state.manifest.chunk_size / 2;
    const int center_row = state.manifest.chunks_tall * state.manifest.chunk_size / 2;
    const float center_col_f = static_cast<float>(center_col);
    const float center_row_f = static_cast<float>(center_row);
    const core::math::Vec2 spawn_pos{center_col_f, center_row_f};

    std::println("[keystone] World ready — spawn at tile ({:.0f}, {:.0f})", spawn_pos.x, spawn_pos.y);
    return WorldLoadInfo{half_tw, half_th, x_origin, spawn_pos};
  }

  // ── configure_dialog_style ──────────────────────────────────────────────────

  void configure_dialog_style(data::RenderState &state, const corundum::core::GameConfig &cfg) {
    const auto &dr = cfg.dialogue_render;
    state.dialog_box.style = corundum::gameplay::ui::DialogBoxStyle{
        .font_id = state.font_id,
        .font_size_speaker = dr.font_size_speaker,
        .font_size_body = dr.font_size_body,
        .font_size_prompt = dr.font_size_prompt,
        .margin = dr.margin,
        .line_spacing = dr.line_spacing,
        .panel_height_frac = dr.panel_height_frac,
        .bg = {10, 10, 20, 220},
        .speaker = {180, 160, 255, 255},
        .body = {220, 220, 220, 255},
        .choice = {180, 180, 180, 255},
        .selected = {255, 230, 100, 255},
    };
  }

  // ── render ───────────────────────────────────────────────────────────────────

  void render(corundum::platform::Renderer &r, data::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::world::Scene &scene, float alpha) {
    const corundum::core::math::Vec2 viewport{cfg.win_w, cfg.win_h};
    const float cam_x = state.prev_cam_x + (scene.camera.x - state.prev_cam_x) * alpha;
    const float cam_y = state.prev_cam_y + (scene.camera.y - state.prev_cam_y) * alpha;

    if (state.mode == data::RenderMode::World) {
      sync_active_chunks(r, state, cfg, scene);

      r.set_world_view({cam_x, cam_y}, viewport);
      for (const auto &chunk : state.active_chunks)
        render_chunk(r, state, chunk, 0, cfg, scene);
      render_entities(r, state, cfg, scene, alpha);

      if (state.chunks_dirty) {
        state.above_z_cache.clear();
        for (const auto &chunk : state.active_chunks)
          for (const int z : chunk.above_z)
            state.above_z_cache.push_back(z);
        std::ranges::sort(state.above_z_cache);
        state.above_z_cache.erase(std::ranges::unique(state.above_z_cache).begin(), state.above_z_cache.end());
        state.chunks_dirty = false;
      }

      for (const int z : state.above_z_cache) {
        r.set_world_view({cam_x, cam_y}, viewport);
        for (const auto &chunk : state.active_chunks)
          render_chunk(r, state, chunk, z, cfg, scene);
      }
    } else {
      r.set_world_view({cam_x, cam_y}, viewport);
      render_tilemap(r, state, 0, cfg, scene);
      render_entities(r, state, cfg, scene, alpha);

      for (const int z : state.map_data.above_z) {
        r.set_world_view({cam_x, cam_y}, viewport);
        render_tilemap(r, state, z, cfg, scene);
      }
    }

    r.reset_screen_view();
    corundum::gameplay::ui::dialog_box_update(state.dialog_box, scene.dialogue, scene.flags, r, viewport);
    corundum::gameplay::ui::dialog_box_render(state.dialog_box, r);
  }

  int first_chunk_tile_px(const data::RenderState &state) noexcept {
    if (state.active_chunks.empty() || state.active_chunks[0].tilemap.tilesets.empty())
      return 0;
    return state.active_chunks[0].tilemap.tilesets[0].info.tile_width;
  }

  // ── load_chunk_entry (internal) ──────────────────────────────────────────────

  static std::optional<data::ChunkEntry> load_chunk_entry(corundum::platform::Renderer &r, data::RenderState &state,
                                                          corundum::gameplay::world::tilemap::ChunkCoord c) {
    auto tm_result = corundum::gameplay::world::tilemap::load_tilemap(state.manifest.chunk_path(c));
    if (!tm_result)
      return std::nullopt;
    corundum::gameplay::world::tilemap::Tilemap tilemap = std::move(*tm_result);

    std::vector<uint32_t> tex_ids;
    tex_ids.reserve(tilemap.tilesets.size());
    for (const auto &ts : tilemap.tilesets) {
      auto result = r.load_texture(ts.info.path);
      if (result.has_value()) {
        tex_ids.push_back(result.value());
      } else {
        std::println(stderr, "[renderer] WARNING: could not load texture '{}'", ts.info.path);
        tex_ids.push_back(0);
      }
    }

    std::vector<int> above_z;
    for (const auto &layer : tilemap.layers)
      if (layer.z_index > 0)
        above_z.push_back(layer.z_index);
    std::ranges::sort(above_z);
    above_z.erase(std::ranges::unique(above_z).begin(), above_z.end());

    return data::ChunkEntry{c, std::move(tilemap), std::move(tex_ids), std::move(above_z)};
  }

  static void rebuild_collision(data::RenderState &state, const corundum::core::GameConfig &cfg) {
    using namespace corundum::gameplay::world::tilemap;
    state.agg_collisions = {};
    state.agg_triangles = {};
    if (state.active_chunks.empty())
      return;

    const int tile_px = state.active_chunks[0].tilemap.diamond_w();
    for (const auto &entry : state.active_chunks) {
      const auto [ox, oy] = chunk_origin_px(entry.coord, state.manifest, tile_px, cfg.tile_scale);
      const auto &cr = entry.tilemap.collisions;
      for (std::size_t i = 0; i < cr.size(); ++i)
        state.agg_collisions.push_back(cr.cols[i] + ox, cr.rows[i] + oy, cr.col_spans[i], cr.row_spans[i]);
      const auto &ct = entry.tilemap.collision_triangles;
      for (std::size_t i = 0; i < ct.size(); ++i)
        state.agg_triangles.push_back(ct.cols[i] + ox, ct.rows[i] + oy, ct.col_spans[i], ct.row_spans[i], ct.cuts[i]);
    }
  }

  // ── sync_active_chunks (internal) ────────────────────────────────────────────

  static void sync_active_chunks(corundum::platform::Renderer &r, data::RenderState &state,
                                 const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene) {
    using namespace corundum::gameplay::world::tilemap;
    if (state.active_chunks.empty())
      return;

    const int diamond_w = state.active_chunks[0].tilemap.diamond_w();
    const int diamond_h = state.active_chunks[0].tilemap.diamond_h();
    const float half_tw = static_cast<float>(diamond_w) * cfg.tile_scale * 0.5f;
    const float half_th = static_cast<float>(diamond_h) * cfg.tile_scale * 0.5f;
    const auto pos_slot = scene.world.transforms.dense_idx(scene.player);
    const float pc = scene.world.transforms.col[pos_slot];
    const float pr = scene.world.transforms.row[pos_slot];
    // Convert tile-grid position to isometric for chunk-streaming API.
    const float total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                        : state.manifest.chunks_tall * state.manifest.chunk_size;
    const float x_origin = (total_h - 1.f) * half_tw;
    const float iso_x = (pc - pr) * half_tw + x_origin;
    const float iso_y = (pc + pr) * half_th;
    const ChunkCoord center = chunk_at_iso(iso_x, iso_y, state.manifest, half_tw, half_th);

    if (center != state.last_center_chunk) {
      constexpr float k_margin_tiles = 0.02f * 128.f;
      const float col_f = pc;
      const float row_f = pr;
      const float local_col = col_f - static_cast<float>(center.x * state.manifest.chunk_size);
      const float local_row = row_f - static_cast<float>(center.y * state.manifest.chunk_size);
      const float chunk_tiles = static_cast<float>(state.manifest.chunk_size);
      const bool x_ok = (center.x == state.last_center_chunk.x) ||
                        (local_col >= k_margin_tiles && local_col <= chunk_tiles - k_margin_tiles);
      const bool y_ok = (center.y == state.last_center_chunk.y) ||
                        (local_row >= k_margin_tiles && local_row <= chunk_tiles - k_margin_tiles);
      if (x_ok && y_ok)
        state.last_center_chunk = center;
    }

    std::array<ChunkCoord, 9> desired{};
    int desired_count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const ChunkCoord c{state.last_center_chunk.x + dx, state.last_center_chunk.y + dy};
        if (state.manifest.in_bounds(c))
          desired[desired_count++] = c;
      }
    }
    const std::span desired_span{desired.data(), static_cast<std::size_t>(desired_count)};

    const auto in_desired = [&](const data::ChunkEntry &e) {
      return std::ranges::find(desired_span, e.coord) != desired_span.end();
    };

    const bool any_stale = !std::ranges::all_of(state.active_chunks, in_desired);
    std::erase_if(state.active_chunks, [&](const data::ChunkEntry &e) { return !in_desired(e); });

    bool any_new = false;
    for (const ChunkCoord c : desired_span) {
      if (!std::ranges::any_of(state.active_chunks, [&](const data::ChunkEntry &e) { return e.coord == c; })) {
        if (auto entry = load_chunk_entry(r, state, c)) {
          std::println("[keystone] Loading chunk ({}, {})", c.x, c.y);
          state.active_chunks.push_back(std::move(*entry));
          any_new = true;
        }
      }
    }

    if (any_stale || any_new) {
      state.chunks_dirty = true;
      rebuild_collision(state, cfg);
    }
  }

  // ── render_tile_layer (internal, shared by render_tilemap / render_chunk) ───

  /// Context for rendering one layer of a tilemap (single-map or world-chunk).
  struct TileLayerCtx {
    const corundum::gameplay::world::tilemap::Tilemap *tilemap;
    const std::vector<uint32_t> *tex_ids;
    float half_tw;
    float half_th;
    float elev_step;
    float x_origin;
    int chunk_offset_col;
    int chunk_offset_row;
  };

  static void render_tile_layer(corundum::platform::Renderer &r, const TileLayerCtx &ctx, int z_index,
                                const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene) {
    const auto &tilemap = *ctx.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const int depth_max = tilemap.width + tilemap.height - 2;

    for (const auto &layer : tilemap.layers) {
      if (!layer.visible || layer.z_index != z_index)
        continue;

      const auto tiles = layer.view(tilemap.width, tilemap.height);

      for (int depth = 0; depth <= depth_max; ++depth) {
        const int col_lo = std::max(0, depth - (tilemap.height - 1));
        const int col_hi = std::min(tilemap.width - 1, depth);

        for (int col = col_lo; col <= col_hi; ++col) {
          const int row = depth - col;
          const int cell_idx = row * tilemap.width + col;

          corundum::gameplay::world::tilemap::TileId gid;
          if (auto it = layer.animated_cells.find(cell_idx); it != layer.animated_cells.end()) {
            const auto &anim = it->second;
            if (anim.frame_gids.empty()) [[unlikely]]
              continue;
            const int n = static_cast<int>(anim.frame_gids.size());
            const int fidx = static_cast<int>(scene.elapsed_time * anim.fps) % n;
            gid = anim.frame_gids[static_cast<std::size_t>(fidx)];
          } else {
            gid = tiles[row, col];
            if (gid == corundum::gameplay::world::tilemap::k_empty_tile)
              continue;
          }

          const corundum::gameplay::world::tilemap::TilemapTileset *ts =
              corundum::gameplay::world::tilemap::find_tileset(tilemap.tilesets, gid);
          if (!ts) [[unlikely]]
            continue;

          const auto ts_idx = static_cast<std::size_t>(ts - tilemap.tilesets.data());
          const uint32_t tex_id = (*ctx.tex_ids)[ts_idx];
          if (tex_id == 0) [[unlikely]]
            continue;

          const int local_id = static_cast<int>(gid) - static_cast<int>(ts->first_gid);
          const int src_col = local_id % ts->info.columns;
          const int src_row = local_id / ts->info.columns;
          const IntRect src{src_col * ts->info.tile_width, src_row * ts->info.tile_height, ts->info.tile_width,
                            ts->info.tile_height};

          bool flip_x = false;
          bool flip_y = false;
          if (auto fit = layer.flip_flags.find(cell_idx); fit != layer.flip_flags.end()) {
            if (fit->second & corundum::gameplay::world::tilemap::k_flip_h)
              flip_x = true;
            if (fit->second & corundum::gameplay::world::tilemap::k_flip_v)
              flip_y = true;
          }

          const int elev = (!layer.elevation.empty() && cell_idx < static_cast<int>(layer.elevation.size()))
                               ? static_cast<int>(layer.elevation[static_cast<std::size_t>(cell_idx)])
                               : 0;

          const int abs_col = ctx.chunk_offset_col + col;
          const int abs_row = ctx.chunk_offset_row + row;
          const corundum::core::math::Vec2 world_pos = corundum::core::math::tile_to_world(
              abs_col, abs_row, elev, ctx.half_tw, ctx.half_th, ctx.elev_step, ctx.x_origin);

          const float scaled_tw = static_cast<float>(ts->info.tile_width) * cfg.tile_scale;
          const float scaled_th = static_cast<float>(ts->info.tile_height) * cfg.tile_scale;
          r.draw(corundum::platform::DrawSprite{
              .texture_id = tex_id,
              .position = {world_pos.x - ts->info.pivot_x * scaled_tw,
                           world_pos.y - (1.f - ts->info.pivot_y) * scaled_th},
              .source = src,
              .scale = {static_cast<float>(cfg.tile_scale), static_cast<float>(cfg.tile_scale)},
              .flip_x = flip_x,
              .flip_y = flip_y,
          });
        }
      }
    }
  }

  static void render_tilemap(corundum::platform::Renderer &r, const data::RenderState &state, int z_index,
                             const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene) {
    const auto &tilemap = state.map_data.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const float half_tw = static_cast<float>(tilemap.diamond_w()) * cfg.tile_scale * 0.5f;
    const float half_th = static_cast<float>(tilemap.diamond_h()) * cfg.tile_scale * 0.5f;
    const TileLayerCtx ctx{&tilemap, &state.map_data.tileset_texture_ids,
                           half_tw,  half_th,
                           0.f,      static_cast<float>(tilemap.height - 1) * half_tw,
                           0,        0};
    render_tile_layer(r, ctx, z_index, cfg, scene);
  }

  static void render_chunk(corundum::platform::Renderer &r, const data::RenderState &state,
                           const data::ChunkEntry &chunk, int z_index, const corundum::core::GameConfig &cfg,
                           const corundum::gameplay::world::Scene &scene) {
    const auto &tilemap = chunk.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const float half_tw = static_cast<float>(tilemap.diamond_w()) * cfg.tile_scale * 0.5f;
    const float half_th = static_cast<float>(tilemap.diamond_h()) * cfg.tile_scale * 0.5f;
    const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                      : state.manifest.chunks_tall * state.manifest.chunk_size;
    const TileLayerCtx ctx{&tilemap,
                           &chunk.tileset_texture_ids,
                           half_tw,
                           half_th,
                           4.f,
                           static_cast<float>(total_h - 1) * half_tw,
                           chunk.coord.x * state.manifest.chunk_size,
                           chunk.coord.y * state.manifest.chunk_size};
    render_tile_layer(r, ctx, z_index, cfg, scene);
  }

  // ── render_entities (internal) ───────────────────────────────────────────────

  static void render_entities(corundum::platform::Renderer &r, data::RenderState &state,
                              const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                              float alpha) {
    const float scale = static_cast<float>(cfg.sprite_scale);

    float half_tw = 0.f;
    float half_th = 8.f;
    float x_origin = 0.f;
    if (!state.active_chunks.empty() && !state.active_chunks[0].tilemap.tilesets.empty()) {
      const auto &tm = state.active_chunks[0].tilemap;
      half_tw = static_cast<float>(tm.diamond_w()) * cfg.tile_scale * 0.5f;
      half_th = static_cast<float>(tm.diamond_h()) * cfg.tile_scale * 0.5f;
      const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                        : state.manifest.chunks_tall * state.manifest.chunk_size;
      x_origin = static_cast<float>(total_h - 1) * half_tw;
    } else if (!state.map_data.tilemap.tilesets.empty()) {
      const auto &tm = state.map_data.tilemap;
      half_tw = static_cast<float>(tm.diamond_w()) * cfg.tile_scale * 0.5f;
      half_th = static_cast<float>(tm.diamond_h()) * cfg.tile_scale * 0.5f;
      x_origin = static_cast<float>(tm.height - 1) * half_tw;
    }

    state.draw_list.clear();

    const auto &transforms = scene.world.transforms;
    const auto &sprites = scene.world.sprites;
    const auto ents = sprites.active_entities();

    [[assume(sprites.count <= std::remove_reference_t<decltype(sprites)>::k_max)]];
    for (uint16_t i = 0; i < sprites.count; ++i) {
      const auto e = ents[i];
      if (!transforms.has(e)) [[unlikely]]
        continue;

      const auto tr_slot = transforms.dense_idx(e);
      float col_f = transforms.col[tr_slot];
      float row_f = transforms.row[tr_slot];

      if (alpha > 0.f && alpha < 1.f && tr_slot < state.prev_col.size()) {
        col_f = state.prev_col[tr_slot] + (col_f - state.prev_col[tr_slot]) * alpha;
        row_f = state.prev_row[tr_slot] + (row_f - state.prev_row[tr_slot]) * alpha;
      }

      const auto result = state.sprite_index.get(sprites.sprite_id[i], sprites.anim_id[i], sprites.frame_index[i]);
      if (!result) [[unlikely]]
        continue;

      const auto &entry = *result;
      const float walk_offset = entry.walk_offset;
      const float iso_x = (col_f - row_f) * half_tw + x_origin;
      const float iso_y = (col_f + row_f) * half_th;
      const float px = iso_x - static_cast<float>(entry.src.width) * scale * 0.5f;
      const float py = iso_y - walk_offset * static_cast<float>(entry.src.height) * scale;
      const float iso_depth = iso_y / half_th;
      state.draw_list.push_back({entry.tex_id, entry.src, px, py, iso_depth});
    }

    std::ranges::sort(state.draw_list,
                      [](const data::DepthEntry &a, const data::DepthEntry &b) noexcept { return a.depth < b.depth; });

    for (const auto &entry : state.draw_list) {
      r.draw(corundum::platform::DrawSprite{
          .texture_id = entry.tex_id,
          .position = {entry.x, entry.y},
          .source = entry.src,
          .scale = {scale, scale},
      });
    }
  }

} // namespace corundum::render::sys
