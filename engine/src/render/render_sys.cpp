#include <corundum/render/render_sys.hpp>

#include <corundum/core/game_config.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/ecs/component/transform_table.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/ui/inventory_panel.hpp>
#include <corundum/gameplay/ui/prompt_box.hpp>
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
#include <iterator>
#include <numeric>
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

namespace corundum::render {

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

} // namespace corundum::render

// ── render free functions ────────────────────────────────────────────────

namespace corundum::render {

  // ── SystemManager equivalents ────────────────────────────────────────────────

  std::expected<void, std::string> initialize(render::RenderState &state) {
    state.draw_list.reserve(corundum::ecs::k_max_entities);
    return {};
  }

  void clean_up(render::RenderState & /*state*/) noexcept {}

  void snapshot_prev_frame(render::RenderState &state, const corundum::gameplay::world::Scene &scene) noexcept {
    const corundum::ecs::TransformTable &transforms = scene.world.transforms;
    const std::uint32_t n = transforms.count;
    for (std::uint32_t i = 0; i < n; ++i) {
      state.prev_col[i] = transforms.col[i];
      state.prev_row[i] = transforms.row[i];
    }
    state.prev_count = n;
    state.prev_cam_x = scene.camera.x;
    state.prev_cam_y = scene.camera.y;
    state.prev_zoom = scene.camera.zoom;
  }

  // ── Internal helpers (forward decls) ─────────────────────────────────────────

  static std::optional<render::ChunkEntry> load_chunk_entry(corundum::platform::Renderer &r, render::RenderState &state,
                                                            corundum::gameplay::world::tilemap::ChunkCoord c,
                                                            const corundum::core::GameConfig &cfg);

  static void sync_active_chunks(render::RenderState &state, const corundum::core::GameConfig &cfg,
                                 const corundum::gameplay::world::Scene &scene);

  static void render_tilemap(corundum::platform::Renderer &r, const render::RenderState &state, int z_index,
                             const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                             float cam_x, float cam_y, float zoom, int window_w, int window_h);

  static void render_chunk(corundum::platform::Renderer &r, const render::RenderState &state,
                           const render::ChunkEntry &chunk, int z_index, const corundum::core::GameConfig &cfg,
                           const corundum::gameplay::world::Scene &scene, float cam_x, float cam_y, float zoom,
                           int window_w, int window_h);

  static void render_ground_layer(corundum::platform::Renderer &r, render::RenderState &state,
                                  const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                                  float alpha, float cam_x, float cam_y, float zoom, int win_w, int win_h);

  // ── load_sprite_index ────────────────────────────────────────────────────────

  void load_sprite_index(corundum::platform::Renderer &r, render::RenderState &state,
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
          const resources::IntPoint origin = frame_origin(*sheet, c);
          state.sprite_index.frame_rects.push_back(IntRect{origin.x, origin.y, fw, fh});
        }
      }
    }
  }

  // ── load_font ────────────────────────────────────────────────────────────────

  std::expected<uint32_t, std::string> load_font(corundum::platform::Renderer &r, render::RenderState &state,
                                                 const std::string &path) {
    auto result = r.load_font(path);
    if (result.has_value())
      state.font_id = result.value();
    return result;
  }

  // ── load_ui_assets ───────────────────────────────────────────────────────────

  std::expected<void, std::string> load_ui_assets(corundum::platform::Renderer &r, render::RenderState &state) {
    constexpr std::string_view k_path = "data/sprite_sheets/ui/borders.json";

    auto j_result = corundum::core::read_json(k_path);
    if (!j_result) {
      std::println(stderr, "[renderer] WARNING: could not load '{}': {}", k_path, j_result.error());
      return {};
    }
    const nlohmann::json &j = *j_result;

    int fw, fh;
    std::string tex_path;
    try {
      fw = j.at("frame_width").get<int>();
      fh = j.at("frame_height").get<int>();
      tex_path = j.at("path").get<std::string>();
    } catch (const nlohmann::json::exception &e) {
      return std::unexpected(std::format("[renderer] malformed UI assets '{}': {}", k_path, e.what()));
    }

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

  std::expected<void, std::string> load_map(corundum::platform::Renderer &r, render::RenderState &state,
                                            const std::string &tilemap_path, const corundum::core::GameConfig &cfg) {
    state.mode = render::RenderMode::SingleMap;
    state.chunks.clear();
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

    const std::string stem = std::filesystem::path(tilemap_path).stem().string();
    const auto portals_file = std::format("{}/{}.json", cfg.paths.portals_dir, stem);
    auto portals = corundum::gameplay::world::load_portals(portals_file);
    if (!portals)
      return std::unexpected(portals.error());
    if (!std::filesystem::exists(portals_file))
      std::println("[engine] 0 portals (no portals file at '{}')", portals_file);
    else
      std::println("[engine] Loaded {} portals from '{}'", portals->size(), portals_file);

    state.map_walkability =
        corundum::gameplay::world::tilemap::build_walkability_graph(tilemap, static_cast<int>(cfg.max_step_height));
    state.map_data = {std::move(tilemap), std::move(tex_ids), std::move(above_z), std::move(*portals)};

    // Upper bound is known at load time (map dims + entity cap), so this is the only
    // reserve draw_list ever needs — steady-state rendering never reallocates it.
    state.draw_list.reserve(static_cast<std::size_t>(state.map_data.tilemap.width) * state.map_data.tilemap.height +
                            corundum::ecs::k_max_entities);
    return {};
  }

  // ── load_world ───────────────────────────────────────────────────────────────

  std::expected<WorldLoadInfo, std::string> load_world(corundum::platform::Renderer &r, render::RenderState &state,
                                                       const corundum::core::GameConfig &cfg,
                                                       const WorldLoadParams &params) {
    using namespace corundum::gameplay::world::tilemap;

    state.mode = render::RenderMode::World;
    state.map_data = {};
    state.map_walkability = {};
    state.chunks.clear();
    state.above_z_cache.clear();

    {
      auto manifest_result = load_world_manifest(cfg.paths.world_manifest_path);
      if (!manifest_result)
        return std::unexpected(manifest_result.error());
      state.manifest = std::move(*manifest_result);
    }
    std::println("[keystone] World manifest: {}×{} chunks of {}×{} tiles", state.manifest.chunks_wide,
                 state.manifest.chunks_tall, state.manifest.chunk_size, state.manifest.chunk_size);

    // Chunk streaming keeps a fixed 3×3 window (radius 1, see sync_active_chunks), so this
    // bound holds for the life of the world regardless of which chunks are currently active.
    constexpr std::size_t k_max_active_chunks = 9;
    state.draw_list.reserve(k_max_active_chunks * static_cast<std::size_t>(state.manifest.chunk_size) *
                                state.manifest.chunk_size +
                            corundum::ecs::k_max_entities);

    // Default spawn is the manifest geometric centre. A supplied spawn (e.g. an interior
    // exit portal back into the overworld) overrides it and re-centres the streaming window
    // on that tile's chunk — otherwise a return near a world edge spawns into an unstreamed
    // chunk. Each coordinate independently falls back to the centre so a half-specified
    // params (only one of spawn_col/spawn_row) is not silently ignored.
    const int cs = state.manifest.chunk_size;
    const int spawn_tile_col = params.spawn_col.value_or(state.manifest.chunks_wide * cs / 2);
    const int spawn_tile_row = params.spawn_row.value_or(state.manifest.chunks_tall * cs / 2);
    const ChunkCoord window_center{spawn_tile_col / cs, spawn_tile_row / cs};
    state.chunks.set_last_center(window_center);
    for (const ChunkCoord c : active_chunk_coords(window_center, 1, state.manifest)) {
      if (auto entry = load_chunk_entry(r, state, c, cfg))
        state.chunks.add_active(std::move(*entry));
    }
    rebuild_collision(state);

    const int diamond_w = state.chunks.active_at(0).tilemap.diamond_w();
    const int diamond_h = state.chunks.active_at(0).tilemap.diamond_h();
    const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                      : state.manifest.chunks_tall * state.manifest.chunk_size;
    const auto iso =
        core::math::compute_isometric_params(diamond_w, diamond_h, total_h, cfg.tile_scale, cfg.elevation_step_px);

    const core::math::Vec2 spawn_pos{static_cast<float>(spawn_tile_col), static_cast<float>(spawn_tile_row)};
    std::println("[keystone] World ready — spawn at tile ({:.0f}, {:.0f})", spawn_pos.x, spawn_pos.y);
    return WorldLoadInfo{iso.half_tw, iso.half_th, iso.x_origin, spawn_pos};
  }

  // ── configure_dialog_style ──────────────────────────────────────────────────

  void configure_dialog_style(render::RenderState &state, const corundum::core::GameConfig &cfg) {
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

  void render(corundum::platform::Renderer &r, render::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::world::Scene &scene, const corundum::gameplay::FlagStore &flags,
              const corundum::gameplay::quest::Registry *quests, const corundum::gameplay::item::Registry *items,
              float alpha, int win_w, int win_h) {
    const corundum::core::math::Vec2 viewport{static_cast<float>(win_w), static_cast<float>(win_h)};
    const float cam_x = state.prev_cam_x + (scene.camera.x - state.prev_cam_x) * alpha;
    const float cam_y = state.prev_cam_y + (scene.camera.y - state.prev_cam_y) * alpha;
    const float zoom = state.prev_zoom + (scene.camera.zoom - state.prev_zoom) * alpha;

    if (state.mode == render::RenderMode::World) {
      sync_active_chunks(state, cfg, scene);

      r.set_world_view({cam_x, cam_y}, viewport, zoom);
      render_ground_layer(r, state, cfg, scene, alpha, cam_x, cam_y, zoom, win_w, win_h);

      if (state.chunks.dirty()) {
        state.above_z_cache.clear();
        for (const auto &chunk : state.chunks.active())
          for (const int z : chunk.above_z)
            state.above_z_cache.push_back(z);
        std::ranges::sort(state.above_z_cache);
        state.above_z_cache.erase(std::ranges::unique(state.above_z_cache).begin(), state.above_z_cache.end());
        state.chunks.clear_dirty();
      }

      for (const int z : state.above_z_cache) {
        r.set_world_view({cam_x, cam_y}, viewport, zoom);
        for (const auto &chunk : state.chunks.active())
          render_chunk(r, state, chunk, z, cfg, scene, cam_x, cam_y, zoom, win_w, win_h);
      }
    } else {
      r.set_world_view({cam_x, cam_y}, viewport, zoom);
      render_ground_layer(r, state, cfg, scene, alpha, cam_x, cam_y, zoom, win_w, win_h);

      for (const int z : state.map_data.above_z) {
        r.set_world_view({cam_x, cam_y}, viewport, zoom);
        render_tilemap(r, state, z, cfg, scene, cam_x, cam_y, zoom, win_w, win_h);
      }
    }

    r.reset_screen_view();
    corundum::gameplay::ui::dialog_box_update(state.dialog_box, scene.dialogue, flags, quests, r, viewport);
    corundum::gameplay::ui::dialog_box_render(state.dialog_box, r);
    if (scene.transition_prompt && !scene.transition_prompt->declined()) {
      const std::string_view question = scene.transition_prompt->transition().return_to_world ? "Leave?" : "Enter?";
      corundum::gameplay::ui::prompt_box_render(r, state.dialog_box.style, state.dialog_box.border, question,
                                                scene.transition_prompt->confirm_selected(), viewport);
    }
    if (scene.mode == corundum::gameplay::world::GameMode::Inventory && items) {
      corundum::gameplay::ui::inventory_panel_render(r, state.dialog_box.style, state.dialog_box.border,
                                                     corundum::gameplay::ui::build_inventory_lines(flags, *items),
                                                     scene.inventory_cursor, viewport);
    }
  }

  /// Returns the full (untrimmed) frame width of the first tile in the active chunk's first
  /// tileset, in pixels. Used as the isometric diamond width fallback when the tilemap has no
  /// explicit @c iso_diamond_w. Returns 0 when no geometry is available — callers must treat 0 as
  /// "invalid / not yet loaded" and not divide by this value.
  int first_chunk_tile_px(const render::RenderState &state) noexcept {
    if (state.chunks.active_empty() || state.chunks.active_at(0).tilemap.tilesets.empty())
      return 0;
    const auto &info = state.chunks.active_at(0).tilemap.tilesets[0].info;
    return info.tile_full_width.empty() ? 0 : info.tile_full_width[0];
  }

  // ── load_chunk_entry (internal) ──────────────────────────────────────────────

  static std::optional<render::ChunkEntry> load_chunk_entry(corundum::platform::Renderer &r, render::RenderState &state,
                                                            corundum::gameplay::world::tilemap::ChunkCoord c,
                                                            const corundum::core::GameConfig &cfg) {
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

    std::vector<corundum::gameplay::world::Portal> portals;
    {
      const std::string stem = state.manifest.chunk_path(c).stem().string();
      const auto portals_path = std::filesystem::path(cfg.paths.portals_dir) / (stem + ".json");
      auto result = corundum::gameplay::world::load_portals(portals_path);
      if (result.has_value())
        portals = std::move(*result);
      else
        std::println(stderr, "[engine] WARN: portals for chunk {} skipped: {}", stem, result.error());
    }

    return render::ChunkEntry{c, std::move(tilemap), std::move(tex_ids), std::move(above_z), std::move(portals)};
  }

  void rebuild_collision(render::RenderState &state) noexcept {
    using namespace corundum::gameplay::world::tilemap;
    state.agg_collisions = {};
    state.agg_triangles = {};
    if (state.chunks.active_empty())
      return;

    for (const auto &entry : state.chunks.active()) {
      const int ox = entry.coord.col * state.manifest.chunk_size;
      const int oy = entry.coord.row * state.manifest.chunk_size;
      const auto &cr = entry.tilemap.collisions;
      for (std::size_t i = 0; i < cr.size(); ++i)
        state.agg_collisions.push_back(cr.cols[i] + ox, cr.rows[i] + oy, cr.col_spans[i], cr.row_spans[i],
                                       cr.elevations[i]);
      const auto &ct = entry.tilemap.collision_triangles;
      for (std::size_t i = 0; i < ct.size(); ++i)
        state.agg_triangles.push_back(ct.cols[i] + ox, ct.rows[i] + oy, ct.col_spans[i], ct.row_spans[i], ct.cuts[i],
                                      ct.elevations[i]);
    }
  }

  // ── sync_active_chunks (internal) ────────────────────────────────────────────

  static void sync_active_chunks(render::RenderState &state, const corundum::core::GameConfig &cfg,
                                 const corundum::gameplay::world::Scene &scene) {
    using namespace corundum::gameplay::world::tilemap;
    if (state.chunks.active_empty())
      return;

    const int diamond_w = state.chunks.active_at(0).tilemap.diamond_w();
    const int diamond_h = state.chunks.active_at(0).tilemap.diamond_h();
    const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                      : state.manifest.chunks_tall * state.manifest.chunk_size;
    const auto iso =
        core::math::compute_isometric_params(diamond_w, diamond_h, total_h, cfg.tile_scale, cfg.elevation_step_px);
    const auto pos_slot = scene.world.transforms.dense_idx(scene.player);
    const float pc = scene.world.transforms.col[pos_slot];
    const float pr = scene.world.transforms.row[pos_slot];
    const auto [pw_x, pw_y] = core::math::tile_to_world(pc, pr, 0, iso);
    const ChunkCoord center = chunk_at_iso(pw_x, pw_y, state.manifest, iso);

    if (center != state.chunks.last_center()) {
      constexpr float k_margin_tiles = 0.02f * 128.f;
      const float local_col = pc - static_cast<float>(center.col * state.manifest.chunk_size);
      const float local_row = pr - static_cast<float>(center.row * state.manifest.chunk_size);
      const float chunk_tiles = static_cast<float>(state.manifest.chunk_size);
      const bool x_ok = (center.col == state.chunks.last_center().col) ||
                        (local_col >= k_margin_tiles && local_col <= chunk_tiles - k_margin_tiles);
      const bool y_ok = (center.row == state.chunks.last_center().row) ||
                        (local_row >= k_margin_tiles && local_row <= chunk_tiles - k_margin_tiles);
      if (x_ok && y_ok)
        state.chunks.set_last_center(center);
    }

    std::array<ChunkCoord, 9> desired{};
    int desired_count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const ChunkCoord c{state.chunks.last_center().col + dx, state.chunks.last_center().row + dy};
        if (state.manifest.in_bounds(c))
          desired[desired_count++] = c;
      }
    }
    const std::span desired_span{desired.data(), static_cast<std::size_t>(desired_count)};
    const auto in_desired = [&](const render::ChunkEntry &e) {
      return std::ranges::find(desired_span, e.coord) != desired_span.end();
    };

    const bool any_stale = state.chunks.prune_active(in_desired);

    for (const ChunkCoord c : desired_span)
      if (!state.chunks.has(c))
        state.chunks.enqueue_pending(c);

    state.chunks.rebuild_slot_table();

    if (any_stale)
      rebuild_collision(state);
  }

  // ── render_tile_layer (internal, shared by render_tilemap / render_chunk) ───

  /// Parameters for rendering one layer of a tilemap (single-map or world-chunk).
  struct TileRenderParams {
    const corundum::gameplay::world::tilemap::Tilemap *tilemap;
    const std::vector<uint32_t> *tex_ids;
    core::math::IsometricParams iso;
    float camera_x;   ///< Camera top-left x in world pixels (interpolated).
    float camera_y;   ///< Camera top-left y in world pixels (interpolated).
    float viewport_w; ///< Viewport width in world pixels.
    float viewport_h; ///< Viewport height in world pixels.
    int chunk_offset_col;
    int chunk_offset_row;
  };

  /// One fully-resolved tile draw, ready to either draw immediately or collect into a depth-sorted list.
  struct ResolvedTile {
    uint32_t tex_id;
    corundum::core::math::IntRect src;
    corundum::core::math::Vec2 position;
    float scale;
    bool flip_x;
    bool flip_y;
    float depth;   ///< iso_depth_key() value, accounting for elevation.
    int elevation; ///< Raw elevation [0-255]; 0 for flat ground.
  };

  /// Resolves the tile at (col, row) in @p layer to draw data, or std::nullopt if the cell is empty/unrenderable.
  /// Shared by render_tile_layer (immediate draw, z>0 bands) and collect_tile_layer (depth-sorted, z==0 band).
  static std::optional<ResolvedTile> resolve_tile_cell(const TileRenderParams &ctx,
                                                       const corundum::gameplay::world::tilemap::TilemapLayer &layer,
                                                       const corundum::gameplay::world::tilemap::Tilemap &tilemap,
                                                       int col, int row, const corundum::core::GameConfig &cfg,
                                                       const corundum::gameplay::world::Scene &scene) {
    const int cell_idx = row * tilemap.width + col;
    const auto cell_uidx = static_cast<std::size_t>(cell_idx);

    corundum::gameplay::world::tilemap::TileId gid;
    const uint32_t anim_idx = layer.baked_animation_index[cell_uidx];
    if (anim_idx != corundum::gameplay::world::tilemap::TilemapLayer::k_no_animation) {
      const auto &anim = layer.baked_animations[anim_idx];
      if (anim.frame_gids.empty()) [[unlikely]]
        return std::nullopt;
      const auto n = anim.frame_gids.size();
      const auto fidx = static_cast<std::size_t>(static_cast<int>(scene.elapsed_time * anim.fps)) % n;
      gid = anim.frame_gids[fidx];
    } else {
      gid = layer.at(col, row, tilemap.width);
      if (gid == corundum::gameplay::world::tilemap::k_empty_tile)
        return std::nullopt;
    }

    const corundum::gameplay::world::tilemap::TilemapTileset *ts =
        corundum::gameplay::world::tilemap::find_tileset(tilemap.tilesets, gid);
    if (!ts) [[unlikely]]
      return std::nullopt;

    const auto ts_idx = static_cast<std::size_t>(std::distance(tilemap.tilesets.data(), ts));
    const uint32_t tex_id = (*ctx.tex_ids)[ts_idx];
    if (tex_id == 0) [[unlikely]]
      return std::nullopt;

    const auto src = corundum::gameplay::world::tilemap::tile_source_rect(*ts, gid);

    const uint8_t flags = layer.baked_flip_flags[cell_uidx];
    const bool flip_x = (flags & corundum::gameplay::world::tilemap::k_flip_h) != 0;
    const bool flip_y = (flags & corundum::gameplay::world::tilemap::k_flip_v) != 0;

    const int elev = (!layer.elevation.empty() && cell_idx < static_cast<int>(layer.elevation.size()))
                         ? static_cast<int>(layer.elevation[static_cast<std::size_t>(cell_idx)])
                         : 0;

    const int abs_col = ctx.chunk_offset_col + col;
    const int abs_row = ctx.chunk_offset_row + row;
    const corundum::core::math::Vec2 world_pos = corundum::core::math::tile_to_world(
        abs_col, abs_row, elev, ctx.iso.half_tw, ctx.iso.half_th, ctx.iso.elev_step, ctx.iso.x_origin);

    // Pivot is always measured against the full (untrimmed) frame, not the trimmed size, so sprites
    // sharing one canvas convention (e.g. a wall body and its separately-authored topper) stay
    // aligned to each other regardless of how much padding either one had trimmed away. Both pivot
    // and frame size are per-tile (TilesetInfo::tile_pivot_*/tile_full_*) since spritepacker computes
    // them per sprite — there's no tileset-wide default to fall back to.
    const int local_id = static_cast<int>(gid) - static_cast<int>(ts->first_gid);
    const auto frame = corundum::gameplay::world::tilemap::get_tile_frame_offset(ts->info, local_id);
    const auto pivot = corundum::gameplay::world::tilemap::get_tile_pivot(ts->info, local_id);
    const float scaled_tw = static_cast<float>(frame.full_width) * cfg.tile_scale;
    const float scaled_th = static_cast<float>(frame.full_height) * cfg.tile_scale;
    const float trim_x_px = static_cast<float>(frame.trim_x) * cfg.tile_scale;
    const float trim_y_px = static_cast<float>(frame.trim_y) * cfg.tile_scale;
    const float depth =
        corundum::core::math::iso_depth_key(static_cast<float>(abs_col), static_cast<float>(abs_row),
                                            static_cast<float>(elev), ctx.iso.half_th, ctx.iso.elev_step);

    return ResolvedTile{
        .tex_id = tex_id,
        .src = src,
        // Anchor at the cell center (tile_to_world + half_th, since tile_to_world returns the cell's
        // top vertex under this codebase's TOP-vertex projection convention), then shift by the trim
        // rect's own offset within the full frame — only the trimmed region is drawn.
        .position = {world_pos.x - pivot.x * scaled_tw + trim_x_px,
                     world_pos.y + ctx.iso.half_th - core::math::pivot_top_offset(pivot.y, scaled_th) + trim_y_px},
        .scale = static_cast<float>(cfg.tile_scale),
        .flip_x = flip_x,
        .flip_y = flip_y,
        .depth = depth,
        .elevation = elev,
    };
  }

  /// Computes cull bounds for one tile layer, expanding the camera rect by per-side
  /// art-extent pads so tiles whose anchors fall outside the viewport but whose sprite
  /// art extends into it aren't skipped.
  static core::math::IsometricCullBounds
  compute_layer_cull_bounds(const TileRenderParams &params,
                            const corundum::gameplay::world::tilemap::TilemapLayer &layer,
                            const corundum::gameplay::world::tilemap::Tilemap &tilemap, float tile_scale) {
    if (params.viewport_w <= 0.f || params.viewport_h <= 0.f)
      return {std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::max() / 2, -1e30f, 1e30f};

    const float max_fw = static_cast<float>(tilemap.max_tile_full_w) * tile_scale;
    const float max_fh = static_cast<float>(tilemap.max_tile_full_h) * tile_scale;
    // Half the diamond height — matches resolve_tile_cell()'s anchor offset so cull padding tracks
    // where the sprite actually sits, not the cell's bottom vertex.
    const float cell_height = params.iso.half_th;
    const float elevation_pad = static_cast<float>(layer.max_elevation) * params.iso.elev_step;

    const float padded_left = params.camera_x - max_fw;
    const float padded_top = params.camera_y - (cell_height + max_fh);
    const float padded_right = params.camera_x + params.viewport_w + max_fw;
    const float padded_bottom = params.camera_y + params.viewport_h + max_fh - cell_height + elevation_pad;

    return core::math::compute_isometric_cull_bounds(padded_left, padded_top, padded_right, padded_bottom, params.iso);
  }

  static void render_tile_layer(corundum::platform::Renderer &r, const TileRenderParams &ctx, int z_index,
                                const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene) {
    const auto &tilemap = *ctx.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const int depth_max = tilemap.width + tilemap.height - 2;

    for (const auto &layer : tilemap.layers) {
      if (!layer.visible || layer.z_index != z_index || layer.depth_sorted)
        continue;

      const core::math::IsometricCullBounds cull = compute_layer_cull_bounds(ctx, layer, tilemap, cfg.tile_scale);
      const int offset_depth = ctx.chunk_offset_col + ctx.chunk_offset_row;

      for (int depth = std::max(0, cull.depth_min - offset_depth);
           depth <= std::min(depth_max, cull.depth_max - offset_depth); ++depth) {
        int col_lo = std::max(0, depth - (tilemap.height - 1));
        int col_hi = std::min(tilemap.width - 1, depth);
        col_lo = std::max(col_lo,
                          core::math::isometric_cull_first_column(cull, depth + offset_depth) - ctx.chunk_offset_col);
        col_hi =
            std::min(col_hi, core::math::isometric_cull_last_column(cull, depth + offset_depth) - ctx.chunk_offset_col);

        for (int col = col_lo; col <= col_hi; ++col) {
          const int row = depth - col;
          const auto rt = resolve_tile_cell(ctx, layer, tilemap, col, row, cfg, scene);
          if (!rt)
            continue;

          r.draw(corundum::platform::DrawSprite{
              .texture_id = rt->tex_id,
              .position = rt->position,
              .source = rt->src,
              .scale = {rt->scale, rt->scale},
              .flip_x = rt->flip_x,
              .flip_y = rt->flip_y,
          });
        }
      }
    }
  }

  /// Same iteration as render_tile_layer, but splits tiles by elevation: flat ground (elevation 0, the vast
  /// majority of tiles) draws immediately, unconditionally beneath entities — exactly the old behavior, so
  /// flat maps are visually unaffected. Only elevated tiles (elevation > 0) are collected into @p out to be
  /// depth-sorted against entities, since only those need occlusion interaction (see design checklist §2).
  /// A flat tile can never be taller than the diamond it occupies, so it can never occlude a taller entity
  /// sprite standing nearby; an elevated tile can, which is the whole point of this fix.
  static void collect_tile_layer(corundum::platform::Renderer &r, const TileRenderParams &ctx, int z_index,
                                 const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                                 std::vector<render::DepthEntry> &out) {
    const auto &tilemap = *ctx.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const int depth_max = tilemap.width + tilemap.height - 2;

    for (const auto &layer : tilemap.layers) {
      if (!layer.visible || layer.z_index != z_index)
        continue;

      const core::math::IsometricCullBounds cull = compute_layer_cull_bounds(ctx, layer, tilemap, cfg.tile_scale);
      const int offset_depth = ctx.chunk_offset_col + ctx.chunk_offset_row;

      for (int depth = std::max(0, cull.depth_min - offset_depth);
           depth <= std::min(depth_max, cull.depth_max - offset_depth); ++depth) {
        int col_lo = std::max(0, depth - (tilemap.height - 1));
        int col_hi = std::min(tilemap.width - 1, depth);
        col_lo = std::max(col_lo,
                          core::math::isometric_cull_first_column(cull, depth + offset_depth) - ctx.chunk_offset_col);
        col_hi =
            std::min(col_hi, core::math::isometric_cull_last_column(cull, depth + offset_depth) - ctx.chunk_offset_col);

        for (int col = col_lo; col <= col_hi; ++col) {
          const int row = depth - col;
          const auto rt = resolve_tile_cell(ctx, layer, tilemap, col, row, cfg, scene);
          if (!rt)
            continue;

          if (rt->elevation <= 0) {
            r.draw(corundum::platform::DrawSprite{
                .texture_id = rt->tex_id,
                .position = rt->position,
                .source = rt->src,
                .scale = {rt->scale, rt->scale},
                .flip_x = rt->flip_x,
                .flip_y = rt->flip_y,
            });
            continue;
          }

          out.push_back({
              .tex_id = rt->tex_id,
              .src = rt->src,
              .x = rt->position.x,
              .y = rt->position.y,
              .depth = rt->depth,
              .scale = rt->scale,
              .flip_x = rt->flip_x,
              .flip_y = rt->flip_y,
          });
        }
      }
    }
  }

  /// Collects tiles from every z_index>0, depth_sorted==true layer into @p out, unconditionally —
  /// unlike collect_tile_layer's ground-layer elevation<=0 fast path, an overlay tile's occlusion
  /// potential comes from its art/pivot (a wall body taller than one cell), not its terrain-elevation
  /// value, so there is no "this tile can't occlude anything" case to skip. Every matching tile always
  /// goes into @p out to be depth-sorted against entities and elevated ground tiles.
  static void collect_sorted_overlay_layers(const TileRenderParams &ctx, const corundum::core::GameConfig &cfg,
                                            const corundum::gameplay::world::Scene &scene,
                                            std::vector<render::DepthEntry> &out) {
    const auto &tilemap = *ctx.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const int depth_max = tilemap.width + tilemap.height - 2;

    for (const auto &layer : tilemap.layers) {
      if (!layer.visible || layer.z_index <= 0 || !layer.depth_sorted)
        continue;

      const core::math::IsometricCullBounds cull = compute_layer_cull_bounds(ctx, layer, tilemap, cfg.tile_scale);
      const int offset_depth = ctx.chunk_offset_col + ctx.chunk_offset_row;

      for (int depth = std::max(0, cull.depth_min - offset_depth);
           depth <= std::min(depth_max, cull.depth_max - offset_depth); ++depth) {
        int col_lo = std::max(0, depth - (tilemap.height - 1));
        int col_hi = std::min(tilemap.width - 1, depth);
        col_lo = std::max(col_lo,
                          core::math::isometric_cull_first_column(cull, depth + offset_depth) - ctx.chunk_offset_col);
        col_hi =
            std::min(col_hi, core::math::isometric_cull_last_column(cull, depth + offset_depth) - ctx.chunk_offset_col);

        for (int col = col_lo; col <= col_hi; ++col) {
          const int row = depth - col;
          const auto rt = resolve_tile_cell(ctx, layer, tilemap, col, row, cfg, scene);
          if (!rt)
            continue;

          out.push_back({
              .tex_id = rt->tex_id,
              .src = rt->src,
              .x = rt->position.x,
              .y = rt->position.y,
              .depth = rt->depth,
              .scale = rt->scale,
              .flip_x = rt->flip_x,
              .flip_y = rt->flip_y,
          });
        }
      }
    }
  }

  /// Collects z_index==0 tiles from the single-map tilemap into @p out. Mirrors render_tilemap's context setup.
  static void collect_ground_tiles_map(corundum::platform::Renderer &r, const render::RenderState &state,
                                       const corundum::core::GameConfig &cfg,
                                       const corundum::gameplay::world::Scene &scene,
                                       std::vector<render::DepthEntry> &out, float cam_x, float cam_y, float zoom,
                                       int window_w, int window_h) {
    const auto &tilemap = state.map_data.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
    const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
    const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                          cfg.tile_scale, cfg.elevation_step_px);
    const TileRenderParams ctx{&tilemap, &state.map_data.tileset_texture_ids, iso, cam_x, cam_y, vp_w, vp_h, 0, 0};
    collect_tile_layer(r, ctx, 0, cfg, scene, out);
  }

  /// Collects z_index==0 tiles from every active chunk into @p out. Mirrors render_chunk's context setup.
  static void collect_ground_tiles_chunks(corundum::platform::Renderer &r, const render::RenderState &state,
                                          const corundum::core::GameConfig &cfg,
                                          const corundum::gameplay::world::Scene &scene,
                                          std::vector<render::DepthEntry> &out, float cam_x, float cam_y, float zoom,
                                          int window_w, int window_h) {
    const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
    const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
    for (const auto &chunk : state.chunks.active()) {
      const auto &tilemap = chunk.tilemap;
      if (tilemap.tilesets.empty())
        continue;

      const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                        : state.manifest.chunks_tall * state.manifest.chunk_size;
      const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_h,
                                                            cfg.tile_scale, cfg.elevation_step_px);
      const TileRenderParams ctx{&tilemap,
                                 &chunk.tileset_texture_ids,
                                 iso,
                                 cam_x,
                                 cam_y,
                                 vp_w,
                                 vp_h,
                                 chunk.coord.col * state.manifest.chunk_size,
                                 chunk.coord.row * state.manifest.chunk_size};
      collect_tile_layer(r, ctx, 0, cfg, scene, out);
    }
  }

  /// Collects depth_sorted overlay-layer tiles from the single-map tilemap into @p out. Mirrors
  /// collect_ground_tiles_map's context setup.
  static void collect_sorted_overlay_map(const render::RenderState &state, const corundum::core::GameConfig &cfg,
                                         const corundum::gameplay::world::Scene &scene,
                                         std::vector<render::DepthEntry> &out, float cam_x, float cam_y, float zoom,
                                         int window_w, int window_h) {
    const auto &tilemap = state.map_data.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
    const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
    const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                          cfg.tile_scale, cfg.elevation_step_px);
    const TileRenderParams ctx{&tilemap, &state.map_data.tileset_texture_ids, iso, cam_x, cam_y, vp_w, vp_h, 0, 0};
    collect_sorted_overlay_layers(ctx, cfg, scene, out);
  }

  /// Collects depth_sorted overlay-layer tiles from every active chunk into @p out. Mirrors
  /// collect_ground_tiles_chunks's context setup.
  static void collect_sorted_overlay_chunks(const render::RenderState &state, const corundum::core::GameConfig &cfg,
                                            const corundum::gameplay::world::Scene &scene,
                                            std::vector<render::DepthEntry> &out, float cam_x, float cam_y, float zoom,
                                            int window_w, int window_h) {
    const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
    const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
    for (const auto &chunk : state.chunks.active()) {
      const auto &tilemap = chunk.tilemap;
      if (tilemap.tilesets.empty())
        continue;

      const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                        : state.manifest.chunks_tall * state.manifest.chunk_size;
      const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_h,
                                                            cfg.tile_scale, cfg.elevation_step_px);
      const TileRenderParams ctx{&tilemap,
                                 &chunk.tileset_texture_ids,
                                 iso,
                                 cam_x,
                                 cam_y,
                                 vp_w,
                                 vp_h,
                                 chunk.coord.col * state.manifest.chunk_size,
                                 chunk.coord.row * state.manifest.chunk_size};
      collect_sorted_overlay_layers(ctx, cfg, scene, out);
    }
  }

  static void render_tilemap(corundum::platform::Renderer &r, const render::RenderState &state, int z_index,
                             const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                             float cam_x, float cam_y, float zoom, int window_w, int window_h) {
    const auto &tilemap = state.map_data.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
    const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
    const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                          cfg.tile_scale, cfg.elevation_step_px);
    const TileRenderParams ctx{&tilemap, &state.map_data.tileset_texture_ids, iso, cam_x, cam_y, vp_w, vp_h, 0, 0};
    render_tile_layer(r, ctx, z_index, cfg, scene);
  }

  static void render_chunk(corundum::platform::Renderer &r, const render::RenderState &state,
                           const render::ChunkEntry &chunk, int z_index, const corundum::core::GameConfig &cfg,
                           const corundum::gameplay::world::Scene &scene, float cam_x, float cam_y, float zoom,
                           int window_w, int window_h) {
    const auto &tilemap = chunk.tilemap;
    if (tilemap.tilesets.empty())
      return;

    const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
    const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
    const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                      : state.manifest.chunks_tall * state.manifest.chunk_size;
    const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_h,
                                                          cfg.tile_scale, cfg.elevation_step_px);
    const TileRenderParams ctx{&tilemap,
                               &chunk.tileset_texture_ids,
                               iso,
                               cam_x,
                               cam_y,
                               vp_w,
                               vp_h,
                               chunk.coord.col * state.manifest.chunk_size,
                               chunk.coord.row * state.manifest.chunk_size};
    render_tile_layer(r, ctx, z_index, cfg, scene);
  }

  /// Elevation of the tile under (col_f, row_f), resolving world-mode chunk ownership as needed.
  /// Returns 0 if no tilemap is loaded there (e.g. entity outside the loaded chunk radius).
  ///
  /// Single-map mode interpolates smoothly across a ramp cell (via interpolated_elevation_at)
  /// so crossing one doesn't pop; chunked/streamed World mode keeps the discrete elevation_at()
  /// lift for now — wiring ramp smoothing into chunked mode is a separate follow-up.
  bool load_one_pending_chunk(corundum::platform::Renderer &r, render::RenderState &state,
                              const corundum::core::GameConfig &cfg) {
    corundum::gameplay::world::tilemap::ChunkCoord c;
    if (!state.chunks.pop_pending(c))
      return false;
    if (auto entry = load_chunk_entry(r, state, c, cfg)) {
      std::println("[keystone] Loading chunk ({}, {})", c.col, c.row);
      state.chunks.add_active(std::move(*entry));
      rebuild_collision(state);
      return true;
    }
    return false;
  }

  float elevation_under(const render::RenderState &state, float col_f, float row_f) noexcept {
    using corundum::gameplay::world::tilemap::elevation_at;
    using corundum::gameplay::world::tilemap::interpolated_elevation_at;

    if (!state.chunks.active_empty()) {
      const int chunk_size = state.manifest.chunk_size;
      if (chunk_size <= 0)
        return 0.f;
      // std::floor (not truncate) so a fractionally-negative col_f/row_f identifies
      // the cell the player is actually in — the same convention as chunk_at_iso,
      // picking, and tilesmith. See std::floor sweep in the world-tilemap audit.
      const int col = static_cast<int>(std::floor(col_f));
      const int row = static_cast<int>(std::floor(row_f));
      const corundum::gameplay::world::tilemap::ChunkCoord owner{
          static_cast<int>(std::floor(static_cast<float>(col) / static_cast<float>(chunk_size))),
          static_cast<int>(std::floor(static_cast<float>(row) / static_cast<float>(chunk_size)))};
      const int dx = owner.col - state.chunks.last_center().col;
      const int dy = owner.row - state.chunks.last_center().row;
      const int32_t slot = state.chunks.slot_at_offset(dx, dy);
      if (slot < 0)
        return 0.f;
      const auto &entry = state.chunks.active_at(static_cast<std::size_t>(slot));
      return static_cast<float>(
          elevation_at(entry.tilemap, col - owner.col * chunk_size, row - owner.row * chunk_size));
    }

    if (!state.map_data.tilemap.tilesets.empty())
      return interpolated_elevation_at(state.map_data.tilemap, col_f, row_f);

    return 0.f;
  }

  // ── render_ground_layer (internal) ───────────────────────────────────────────

  /// Fallback half diamond height used when no tilemap is loaded yet (ISO diamond_h default for 32×32 tiles at 1×).
  constexpr float k_default_half_th = 8.f;

  /// Draws the z_index==0 tile band and all entities. Flat ground (elevation 0) draws immediately,
  /// unconditionally beneath entities — a flat tile can never be taller than its own diamond, so it can
  /// never legitimately occlude a taller entity sprite standing near it; this preserves the old two-pass
  /// behavior exactly for flat maps. Elevated tiles (elevation > 0) are depth-sorted together with entities
  /// (see iso_depth_key) so a raised platform correctly occludes/is-occluded-by nearby entities and tiles.
  /// z_index>0 layers remain a separate, subsequent immediate-draw pass (always above entities).
  static void render_ground_layer(corundum::platform::Renderer &r, render::RenderState &state,
                                  const corundum::core::GameConfig &cfg, const corundum::gameplay::world::Scene &scene,
                                  float alpha, float cam_x, float cam_y, float zoom, int win_w, int win_h) {
    const float scale = cfg.character_scale;

    core::math::IsometricParams iso{};
    if (!state.chunks.active_empty() && !state.chunks.active_at(0).tilemap.tilesets.empty()) {
      const auto &tm = state.chunks.active_at(0).tilemap;
      const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                        : state.manifest.chunks_tall * state.manifest.chunk_size;
      iso = core::math::compute_isometric_params(tm.diamond_w(), tm.diamond_h(), total_h, cfg.tile_scale,
                                                 cfg.elevation_step_px);
    } else if (!state.map_data.tilemap.tilesets.empty()) {
      const auto &tm = state.map_data.tilemap;
      iso = core::math::compute_isometric_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale,
                                                 cfg.elevation_step_px);
    }
    if (iso.half_th == 0.f)
      iso.half_th = k_default_half_th;

    state.draw_list.clear();

    if (!state.chunks.active_empty())
      collect_ground_tiles_chunks(r, state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);
    else
      collect_ground_tiles_map(r, state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);

    if (!state.chunks.active_empty())
      collect_sorted_overlay_chunks(state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);
    else
      collect_sorted_overlay_map(state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);

    const auto &transforms = scene.world.transforms;
    const auto &sprites = scene.world.sprites;
    const auto ents = sprites.active_entities();

    const float vp_r = cam_x + static_cast<float>(win_w) / zoom;
    const float vp_b = cam_y + static_cast<float>(win_h) / zoom;

    [[assume(sprites.count <= std::remove_reference_t<decltype(sprites)>::k_max)]];
    for (uint16_t i = 0; i < sprites.count; ++i) {
      const auto e = ents[i];
      if (!transforms.has(e)) [[unlikely]]
        continue;

      const auto tr_slot = transforms.dense_idx(e);
      float col_f = transforms.col[tr_slot];
      float row_f = transforms.row[tr_slot];

      if (alpha > 0.f && alpha < 1.f && tr_slot < state.prev_count) {
        col_f = state.prev_col[tr_slot] + (col_f - state.prev_col[tr_slot]) * alpha;
        row_f = state.prev_row[tr_slot] + (row_f - state.prev_row[tr_slot]) * alpha;
      }

      const auto result = state.sprite_index.get(sprites.sprite_id[i], sprites.anim_id[i], sprites.frame_index[i]);
      if (!result) [[unlikely]]
        continue;

      const auto &entry = *result;
      const float walk_offset = entry.walk_offset;
      const float elev = elevation_under(state, col_f, row_f);
      // Entity anchor = cell center (top vertex + half_th), matching the cell-center
      // anchor convention used for tile sprites — keeps actors on the ground instead
      // of floating half_th above it.
      const auto [ex, ey] = core::math::tile_to_world_center(col_f, row_f, elev, iso);
      const float px = ex - static_cast<float>(entry.src.width) * scale * 0.5f;
      const float py = ey - walk_offset * static_cast<float>(entry.src.height) * scale;

      // AABB cull — skip entities entirely outside the visible viewport
      const float sw = static_cast<float>(entry.src.width) * scale;
      const float sh = static_cast<float>(entry.src.height) * scale;
      if (px + sw < cam_x || px > vp_r || py + sh < cam_y || py > vp_b)
        continue;

      const float iso_depth = corundum::core::math::iso_depth_key(col_f, row_f, elev, iso.half_th, iso.elev_step);
      state.draw_list.push_back(
          {.tex_id = entry.tex_id, .src = entry.src, .x = px, .y = py, .depth = iso_depth, .scale = scale});
    }

    // Sort indices, not the 40-byte DepthEntry structs: std::ranges::sort is introsort (in-place,
    // no O(n) aux buffer), unlike stable_sort. Depth ties only occur for flat tiles on integer
    // anti-diagonals — not for entities at fractional positions — so stability was barely exercised;
    // an unstable sort on 4-byte indices is both cheaper to move and allocation-free. draw_order is
    // reused across frames (resized, never freed) so this touches no per-frame heap allocation.
    state.draw_order.resize(state.draw_list.size());
    std::iota(state.draw_order.begin(), state.draw_order.end(), 0u);
    std::ranges::sort(state.draw_order, {}, [&](uint32_t i) noexcept { return state.draw_list[i].depth; });

    for (const uint32_t idx : state.draw_order) {
      const auto &entry = state.draw_list[idx];
      r.draw(corundum::platform::DrawSprite{
          .texture_id = entry.tex_id,
          .position = {entry.x, entry.y},
          .source = entry.src,
          .scale = {entry.scale, entry.scale},
          .flip_x = entry.flip_x,
          .flip_y = entry.flip_y,
      });
    }
  }

} // namespace corundum::render
