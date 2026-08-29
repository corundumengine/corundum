#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>
#pragma GCC diagnostic pop

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

using corundum::core::math::compute_isometric_params;
using corundum::core::math::IntRect;
using corundum::core::math::iso_depth_key;
using corundum::core::math::IsometricParams;
using corundum::core::math::pivot_top_offset;
using corundum::core::math::tile_to_world;
using corundum::core::math::Vec2;
using corundum::gameplay::world::tilemap::find_tileset;
using corundum::gameplay::world::tilemap::get_tile_frame_offset;
using corundum::gameplay::world::tilemap::get_tile_pivot;
using corundum::gameplay::world::tilemap::k_empty_tile;
using corundum::gameplay::world::tilemap::load_tilemap;
using corundum::gameplay::world::tilemap::tile_source_rect;
using corundum::gameplay::world::tilemap::TileFrameOffset;
using corundum::gameplay::world::tilemap::TileId;
using corundum::gameplay::world::tilemap::Tilemap;
using corundum::gameplay::world::tilemap::TilemapLayer;
using corundum::gameplay::world::tilemap::TilemapTileset;
using corundum::gameplay::world::tilemap::TilePivot;

namespace {

  struct Args {
    std::string world{"game/data/world"};
    std::string output{"map.png"};
    float scale{0.25f};
    bool labels{false};
  };

  void print_usage(const char *prog) {
    std::println("Usage: {} [options]", prog);
    std::println("  --world   <dir>   World data directory (default: game/data/world)");
    std::println("  --output  <path>  Output PNG path     (default: map.png)");
    std::println("  --scale   <f>     Render scale factor (default: 0.25 = quarter atlas size)");
    std::println("  --labels          Draw chunk names on the map");
  }

  std::optional<Args> parse_args(int argc, char **argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
      const std::string flag{argv[i]};
      if ((flag == "--world" || flag == "--output" || flag == "--scale") && i + 1 < argc) {
        const std::string val{argv[++i]};
        if (flag == "--world")
          args.world = val;
        else if (flag == "--output")
          args.output = val;
        else
          args.scale = std::stof(val);
      } else if (flag == "--labels") {
        args.labels = true;
      } else {
        std::println(stderr, "Unknown argument: {}", flag);
        return std::nullopt;
      }
    }
    return args;
  }

  /// A decoded atlas PNG (RGBA) referenced by one or more tilesets.
  struct AtlasImage {
    std::vector<uint8_t> pixels;
    int w{};
    int h{};
  };

  /// One resolved, drawable tile sprite.
  struct DrawItem {
    int image_slot{}; ///< Index into the (fully-loaded) @c images vector; all images are loaded
                      ///< before any drawing, so indexing by slot is stable across reallocation.
    IntRect src{};
    float pos_x{};
    float pos_y{};
    float scale{};
    bool flip_x{};
    bool flip_y{};
    float depth{};
    bool above{}; ///< Non-depth-sorted z>0 layer (roofs) drawn on top of everything.
  };

  /// Loads (cached) the atlas image for @p png_path, storing it in @p images. All images are loaded
  /// before any drawing begins, so indexing @p images by the returned slot is stable. Returns the
  /// image slot, or std::nullopt on failure.
  std::optional<int> load_image(std::unordered_map<std::string, int> &slot_by_path, std::vector<AtlasImage> &images,
                                const std::string &png_path) {
    if (auto it = slot_by_path.find(png_path); it != slot_by_path.end())
      return it->second;

    int w{};
    int h{};
    int channels{};
    stbi_uc *data = stbi_load(png_path.c_str(), &w, &h, &channels, 4);
    if (!data) {
      std::println(stderr, "Warning: cannot load PNG: {} ({})", png_path, stbi_failure_reason());
      return std::nullopt;
    }
    AtlasImage img;
    img.w = w;
    img.h = h;
    img.pixels.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);

    const int slot = static_cast<int>(images.size());
    slot_by_path.emplace(png_path, slot);
    images.push_back(std::move(img));
    return slot;
  }

  /// Resolves a single tile cell to a draw item, mirroring the engine renderer's anchor math
  /// (render_sys.cpp resolve_tile_cell) so the image matches the in-game view.
  std::optional<DrawItem> resolve_tile(const Tilemap &tm, const TilemapLayer &layer, const IsometricParams &iso,
                                       int local_col, int local_row, int world_col, int world_row, float scale,
                                       const std::vector<int> &image_slots) {
    const int cell_idx = static_cast<int>(tm.width) * local_row + local_col;
    const auto cell_uidx = static_cast<std::size_t>(cell_idx);

    TileId gid = layer.tiles[cell_uidx];
    if (gid == k_empty_tile) {
      if (auto it = layer.animated_cells.find(cell_idx);
          it != layer.animated_cells.end() && !it->second.frame_gids.empty())
        gid = it->second.frame_gids[0];
      else
        return std::nullopt;
    }

    const TilemapTileset *ts = find_tileset(tm.tilesets, gid);
    if (!ts)
      return std::nullopt;
    const auto ts_idx = static_cast<std::size_t>(std::distance(tm.tilesets.data(), ts));
    if (ts_idx >= image_slots.size() || image_slots[ts_idx] < 0)
      return std::nullopt;

    const IntRect src = tile_source_rect(*ts, gid);
    if (src.width == 0 || src.height == 0)
      return std::nullopt;

    const std::uint8_t flags = cell_uidx < layer.baked_flip_flags.size() ? layer.baked_flip_flags[cell_uidx] : 0;
    const bool flip_x = (flags & corundum::gameplay::world::tilemap::k_flip_h) != 0;
    const bool flip_y = (flags & corundum::gameplay::world::tilemap::k_flip_v) != 0;

    const int elev =
        (cell_idx < static_cast<int>(layer.elevation.size())) ? static_cast<int>(layer.elevation[cell_uidx]) : 0;

    const Vec2 world_pos =
        tile_to_world(world_col, world_row, elev, iso.half_tw, iso.half_th, iso.elev_step, iso.x_origin);

    const int local_id = static_cast<int>(gid) - static_cast<int>(ts->first_gid);
    const TileFrameOffset frame = get_tile_frame_offset(ts->info, local_id);
    const TilePivot pivot = get_tile_pivot(ts->info, local_id);
    const float scaled_tw = static_cast<float>(frame.full_width) * scale;
    const float scaled_th = static_cast<float>(frame.full_height) * scale;
    const float trim_x_px = static_cast<float>(frame.trim_x) * scale;
    const float trim_y_px = static_cast<float>(frame.trim_y) * scale;
    const float depth = iso_depth_key(static_cast<float>(world_col), static_cast<float>(world_row),
                                      static_cast<float>(elev), iso.half_th, iso.elev_step);

    DrawItem item;
    item.image_slot = image_slots[ts_idx];
    item.src = src;
    item.pos_x = world_pos.x - pivot.x * scaled_tw + trim_x_px;
    item.pos_y = world_pos.y + iso.half_th - pivot_top_offset(pivot.y, scaled_th) + trim_y_px;
    item.scale = scale;
    item.flip_x = flip_x;
    item.flip_y = flip_y;
    item.depth = depth;
    item.above = layer.z_index > 0 && !layer.depth_sorted;
    return item;
  }

  void draw_item(std::vector<uint8_t> &pixels, int out_w, int out_h, const std::vector<AtlasImage> &images,
                 float offset_x, float offset_y, const DrawItem &item) {
    const AtlasImage &image = images[static_cast<std::size_t>(item.image_slot)];
    const int x0 = static_cast<int>(std::floor(item.pos_x + offset_x));
    const int y0 = static_cast<int>(std::floor(item.pos_y + offset_y));
    const float sw = static_cast<float>(item.src.width) * item.scale;
    const float sh = static_cast<float>(item.src.height) * item.scale;
    const int x1 = static_cast<int>(std::ceil(item.pos_x + offset_x + sw));
    const int y1 = static_cast<int>(std::ceil(item.pos_y + offset_y + sh));

    const float inv_scale = item.scale > 0.f ? 1.f / item.scale : 0.f;
    const float base_off_x = item.pos_x + offset_x;
    const float base_off_y = item.pos_y + offset_y;
    const int last_sx = item.src.width - 1;
    const int last_sy = item.src.height - 1;

    for (int py = y0; py < y1; ++py) {
      if (py < 0 || py >= out_h)
        continue;
      int sy = static_cast<int>((static_cast<float>(py) - base_off_y) * inv_scale);
      sy = std::clamp(sy, 0, last_sy);
      const int local_row = item.flip_y ? (last_sy - sy) : sy;
      const int src_row = item.src.y + local_row;
      for (int px = x0; px < x1; ++px) {
        if (px < 0 || px >= out_w)
          continue;
        int sx = static_cast<int>((static_cast<float>(px) - base_off_x) * inv_scale);
        sx = std::clamp(sx, 0, last_sx);
        const int local_col = item.flip_x ? (last_sx - sx) : sx;
        const int src_col = item.src.x + local_col;
        const auto sidx = static_cast<std::size_t>(src_row * image.w + src_col);
        const std::uint8_t a = image.pixels[sidx * 4 + 3];
        if (a < 128)
          continue;
        const auto didx = static_cast<std::size_t>((py * out_w + px) * 4);
        pixels[didx + 0] = image.pixels[sidx * 4 + 0];
        pixels[didx + 1] = image.pixels[sidx * 4 + 1];
        pixels[didx + 2] = image.pixels[sidx * 4 + 2];
        pixels[didx + 3] = 255;
      }
    }
  }

  /// Each quad vertex from stb_easy_font: x, y, z (float) + color (4 bytes).
  struct EasyFontVertex {
    float x{};
    float y{};
    float z{};
    std::uint8_t col[4]{};
  };

  void fill_quads(std::vector<uint8_t> &pixels, int out_w, int out_h, int ox, int oy, const std::string &text,
                  std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    constexpr int k_max_quads = 512;
    std::array<EasyFontVertex, k_max_quads * 4> vbuf{};
    const int num_quads =
        stb_easy_font_print(static_cast<float>(ox), static_cast<float>(oy), const_cast<char *>(text.c_str()), nullptr,
                            vbuf.data(), static_cast<int>(sizeof(vbuf)));
    for (int q = 0; q < num_quads; ++q) {
      const EasyFontVertex *v = &vbuf[static_cast<std::size_t>(q) * 4];
      const int x0 = static_cast<int>(std::ranges::min({v[0].x, v[1].x, v[2].x, v[3].x}));
      const int y0 = static_cast<int>(std::ranges::min({v[0].y, v[1].y, v[2].y, v[3].y}));
      const int x1 = static_cast<int>(std::ranges::max({v[0].x, v[1].x, v[2].x, v[3].x}));
      const int y1 = static_cast<int>(std::ranges::max({v[0].y, v[1].y, v[2].y, v[3].y}));
      for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
          if (px < 0 || px >= out_w || py < 0 || py >= out_h)
            continue;
          const auto idx = static_cast<std::size_t>((py * out_w + px) * 4);
          pixels[idx + 0] = r;
          pixels[idx + 1] = g;
          pixels[idx + 2] = b;
          pixels[idx + 3] = 255;
        }
      }
    }
  }

  void draw_label(std::vector<uint8_t> &pixels, int out_w, int out_h, int cx, int cy, const std::string &text) {
    fill_quads(pixels, out_w, out_h, cx + 1, cy + 1, text, 0, 0, 0);
    fill_quads(pixels, out_w, out_h, cx, cy, text, 255, 255, 255);
  }

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }
  const auto args_opt = parse_args(argc, argv);
  if (!args_opt) {
    print_usage(argv[0]);
    return 1;
  }
  const Args &args = *args_opt;

  const fs::path world_dir{args.world};
  std::ifstream mf(world_dir / "manifest.json");
  if (!mf) {
    std::println(stderr, "Cannot open: {}", (world_dir / "manifest.json").string());
    return 1;
  }
  const json manifest = json::parse(mf);
  const int chunks_wide = manifest.at("chunks_wide").get<int>();
  const int chunks_tall = manifest.at("chunks_tall").get<int>();
  const int chunk_size = manifest.at("chunk_size").get<int>();
  std::println("World: {}×{} chunks, {} tiles/chunk", chunks_wide, chunks_tall, chunk_size);

  // Every chunk shares one tileset table and iso geometry; anchor from chunk_0_0.
  int diamond_w{};
  int diamond_h{};
  {
    const fs::path probe = world_dir / std::format("chunk_{}_{}.json", 0, 0);
    auto tm_result = load_tilemap(probe);
    if (!tm_result) {
      std::println(stderr, "Cannot load {} : {}", probe.string(), tm_result.error());
      return 1;
    }
    diamond_w = tm_result->diamond_w();
    diamond_h = tm_result->diamond_h();
  }

  const int total_tiles_tall = chunks_tall * chunk_size;
  const IsometricParams iso = compute_isometric_params(diamond_w, diamond_h, total_tiles_tall, args.scale,
                                                       /*elev_step_px=*/4.f);

  std::unordered_map<std::string, int> slot_by_path;
  std::vector<AtlasImage> images;
  std::vector<DrawItem> depth_items;
  std::vector<DrawItem> above_items;
  std::vector<std::pair<int, int>> label_pos;

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();

  auto track_bounds = [&](const DrawItem &item) {
    min_x = std::min(min_x, item.pos_x);
    min_y = std::min(min_y, item.pos_y);
    max_x = std::max(max_x, item.pos_x + static_cast<float>(item.src.width) * item.scale);
    max_y = std::max(max_y, item.pos_y + static_cast<float>(item.src.height) * item.scale);
  };

  int rendered{};
  for (int cy = 0; cy < chunks_tall; ++cy) {
    for (int cx = 0; cx < chunks_wide; ++cx) {
      const fs::path chunk_path = world_dir / std::format("chunk_{}_{}.json", cx, cy);
      auto tm_result = load_tilemap(chunk_path);
      if (!tm_result) {
        std::println(stderr, "Warning: cannot load {} : {}", chunk_path.string(), tm_result.error());
        continue;
      }
      const Tilemap &tm = *tm_result;

      std::vector<int> image_slots;
      image_slots.reserve(tm.tilesets.size());
      for (const TilemapTileset &ts : tm.tilesets)
        image_slots.push_back(load_image(slot_by_path, images, ts.info.path).value_or(-1));

      for (const TilemapLayer &layer : tm.layers) {
        if (!layer.visible)
          continue;
        for (int row = 0; row < tm.height; ++row) {
          for (int col = 0; col < tm.width; ++col) {
            const int world_col = cx * chunk_size + col;
            const int world_row = cy * chunk_size + row;
            auto item = resolve_tile(tm, layer, iso, col, row, world_col, world_row, args.scale, image_slots);
            if (!item)
              continue;
            track_bounds(*item);
            if (item->above)
              above_items.push_back(*item);
            else
              depth_items.push_back(*item);
          }
        }
      }

      if (args.labels)
        label_pos.emplace_back(cx, cy);

      ++rendered;
      if (rendered % 32 == 0)
        std::println("  {}/{} chunks...", rendered, chunks_wide * chunks_tall);
    }
  }

  constexpr int k_margin = 4;
  const float offset_x = k_margin - min_x;
  const float offset_y = k_margin - min_y;
  const int out_w = static_cast<int>(std::ceil(max_x - min_x)) + 2 * k_margin;
  const int out_h = static_cast<int>(std::ceil(max_y - min_y)) + 2 * k_margin;
  std::println("Output image: {}×{} px", out_w, out_h);
  if (out_w <= 0 || out_h <= 0) {
    std::println(stderr, "Nothing to render (no tiles found)");
    return 1;
  }

  std::vector<uint8_t> pixels(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0u);

  std::stable_sort(depth_items.begin(), depth_items.end(),
                   [](const DrawItem &a, const DrawItem &b) { return a.depth < b.depth; });
  for (const DrawItem &item : depth_items)
    draw_item(pixels, out_w, out_h, images, offset_x, offset_y, item);
  for (const DrawItem &item : above_items)
    draw_item(pixels, out_w, out_h, images, offset_x, offset_y, item);

  if (args.labels) {
    for (const auto &[cx, cy] : label_pos) {
      const Vec2 base =
          tile_to_world(cx * chunk_size, cy * chunk_size, 0, iso.half_tw, iso.half_th, iso.elev_step, iso.x_origin);
      draw_label(pixels, out_w, out_h, static_cast<int>(base.x + offset_x), static_cast<int>(base.y + offset_y),
                 std::format("chunk_{}_{}", cx, cy));
    }
  }

  std::println("Writing {}...", args.output);
  if (stbi_write_png(args.output.c_str(), out_w, out_h, 4, pixels.data(), out_w * 4) == 0) {
    std::println(stderr, "Failed to write PNG: {}", args.output);
    return 1;
  }
  std::println("Done. {}×{} px written to: {}", out_w, out_h, args.output);
  return 0;
}
