// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <nlohmann/json_fwd.hpp>
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

#include <corundum/core/content_source.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
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

using nlohmann::json;
namespace fs = std::filesystem;

using corundum::core::math::compute_isometric_params;
using corundum::core::math::IntRect;
using corundum::core::math::iso_depth_key;
using corundum::core::math::IsometricParams;
using corundum::core::math::pivot_top_offset;
using corundum::core::math::tile_to_world;
using corundum::core::math::Vec2;
using corundum::world::tilemap::find_tileset;
using corundum::world::tilemap::get_tile_frame_offset;
using corundum::world::tilemap::get_tile_pivot;
using corundum::world::tilemap::k_empty_tile;
using corundum::world::tilemap::load_tilemap;
using corundum::world::tilemap::tile_source_rect;
using corundum::world::tilemap::TileFrameOffset;
using corundum::world::tilemap::TileId;
using corundum::world::tilemap::Tilemap;
using corundum::world::tilemap::TilemapLayer;
using corundum::world::tilemap::TilemapTileset;
using corundum::world::tilemap::TilePivot;

namespace tools::mapview {

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

std::optional<Args> parse_args(int argc, const char *const *argv) {
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
  int image_slot{}; ///< Index into the (fully-loaded) `images` vector; all images are loaded
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

/// Loads (cached) the atlas image for `png_path`, storing it in `images`. All images are loaded
/// before any drawing begins, so indexing `images` by the returned slot is stable. Returns the
/// image slot, or std::nullopt on failure.
std::optional<int> load_image(std::unordered_map<std::string, int> &slot_by_path, std::vector<AtlasImage> &images,
                              const std::string &png_path) {
  if (const auto it = slot_by_path.find(png_path); it != slot_by_path.end())
    return it->second;

  int w{};
  int h{};
  int channels{};
  stbi_uc *data = stbi_load(png_path.c_str(), &w, &h, &channels, 4);
  if (data == nullptr) {
    std::println(stderr, "Warning: cannot load PNG: {} ({})", png_path, stbi_failure_reason());
    return std::nullopt;
  }
  AtlasImage img;
  img.w = w;
  img.h = h;
  img.pixels.assign(data, data + ((static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) * 4));
  stbi_image_free(data);

  const int slot = static_cast<int>(images.size());
  slot_by_path.emplace(png_path, slot);
  images.push_back(std::move(img));
  return slot;
}

/// The tile GID at `cell_index`: the layer's tile, or an animation's first frame; nullopt when empty.
std::optional<TileId> cell_gid(const TilemapLayer &layer, std::size_t cell_index) {
  const TileId tile = layer.tiles[cell_index];
  if (tile != k_empty_tile)
    return tile;
  const auto animation = layer.animated_cells.find(static_cast<int>(cell_index));
  if (animation == layer.animated_cells.end() || animation->second.frame_gids.empty())
    return std::nullopt;
  return animation->second.frame_gids[0];
}

/// A tile cell resolved to the tileset, texture slot and source rect it draws from.
struct TileSource {
  const TilemapTileset *tileset{nullptr};

  IntRect rect{};

  int image_slot{-1};

  TileId gid{k_empty_tile};
};

/// Resolve a cell's GID to its tileset, texture slot and non-empty source rect.
std::optional<TileSource> resolve_tile_source(const Tilemap &tm, const TilemapLayer &layer, int cell_idx,
                                              const std::vector<int> &image_slots) {
  const std::optional<TileId> gid = cell_gid(layer, static_cast<std::size_t>(cell_idx));
  if (!gid)
    return std::nullopt;
  const TilemapTileset *tileset = find_tileset(tm.tilesets, *gid);
  if (tileset == nullptr)
    return std::nullopt;
  const auto index = static_cast<std::size_t>(std::distance(tm.tilesets.data(), tileset));
  if (index >= image_slots.size() || image_slots[index] < 0)
    return std::nullopt;
  const IntRect rect = tile_source_rect(*tileset, *gid);
  if (rect.width == 0 || rect.height == 0)
    return std::nullopt;
  return TileSource{.tileset = tileset, .rect = rect, .image_slot = image_slots[index], .gid = *gid};
}

/// Build a tile's draw item from its resolved source, mirroring the engine renderer's anchor math.
DrawItem make_draw_item(const TilemapLayer &layer, const TileSource &source, int cell_idx, int world_col, int world_row,
                        int elevation, const IsometricParams &iso, float scale) {
  const int local_id = static_cast<int>(source.gid) - static_cast<int>(source.tileset->first_gid);
  const TileFrameOffset frame = get_tile_frame_offset(source.tileset->info, local_id);
  const TilePivot pivot = get_tile_pivot(source.tileset->info, local_id);
  const float scaled_tw = static_cast<float>(frame.full_width) * scale;
  const float scaled_th = static_cast<float>(frame.full_height) * scale;
  const float trim_x_px = static_cast<float>(frame.trim_x) * scale;
  const float trim_y_px = static_cast<float>(frame.trim_y) * scale;
  const Vec2 world_pos = tile_to_world(static_cast<float>(world_col), static_cast<float>(world_row), elevation, iso);
  const float depth = iso_depth_key(static_cast<float>(world_col), static_cast<float>(world_row),
                                    static_cast<float>(elevation), iso.half_th, iso.elev_step);
  const auto flip = layer.flip_flags.find(cell_idx);
  const std::uint8_t flags = flip == layer.flip_flags.end() ? std::uint8_t{0} : flip->second;

  DrawItem item;
  item.image_slot = source.image_slot;
  item.src = source.rect;
  item.pos_x = (world_pos.x - (pivot.x * scaled_tw)) + trim_x_px;
  item.pos_y = world_pos.y + iso.half_th - pivot_top_offset(pivot.y, scaled_th) + trim_y_px;
  item.scale = scale;
  item.flip_x = (flags & corundum::world::tilemap::k_flip_h) != 0;
  item.flip_y = (flags & corundum::world::tilemap::k_flip_v) != 0;
  item.depth = depth;
  item.above = layer.z_index > 0 && !layer.depth_sorted;
  return item;
}

/// Resolves a single tile cell to a draw item, mirroring the engine renderer's anchor math
/// (render_system.cpp resolve_tile_cell) so the image matches the in-game view.
std::optional<DrawItem> resolve_tile(const Tilemap &tm, const TilemapLayer &layer, const IsometricParams &iso,
                                     int local_col, int local_row, int world_col, int world_row, float scale,
                                     const std::vector<int> &image_slots) {
  const int cell_idx = (static_cast<int>(tm.width) * local_row) + local_col;
  const auto source = resolve_tile_source(tm, layer, cell_idx, image_slots);
  if (!source)
    return std::nullopt;
  const int elevation = std::cmp_less(cell_idx, layer.elevation.size())
                            ? static_cast<int>(layer.elevation[static_cast<std::size_t>(cell_idx)])
                            : 0;
  return make_draw_item(layer, *source, cell_idx, world_col, world_row, elevation, iso, scale);
}

/// Mirror a clamped source coordinate onto its flipped counterpart when `flipped` is set.
int flip_coordinate(int coordinate, int last, bool flipped) {
  return flipped ? (last - coordinate) : coordinate;
}

/// Copy one source pixel into `pixels` at `destination_index` unless it is mostly transparent.
void blend_pixel(std::vector<std::uint8_t> &pixels, const std::vector<std::uint8_t> &source, std::size_t source_index,
                 std::size_t destination_index) {
  if (source[source_index + 3] < 128)
    return;
  pixels[destination_index + 0] = source[source_index + 0];
  pixels[destination_index + 1] = source[source_index + 1];
  pixels[destination_index + 2] = source[source_index + 2];
  pixels[destination_index + 3] = 255;
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
    const int local_row = flip_coordinate(sy, last_sy, item.flip_y);
    const int src_row = item.src.y + local_row;
    const std::size_t src_row_base = static_cast<std::size_t>(src_row) * static_cast<std::size_t>(image.w);
    const std::size_t dst_row_base = static_cast<std::size_t>(py) * static_cast<std::size_t>(out_w);
    for (int px = x0; px < x1; ++px) {
      if (px < 0 || px >= out_w)
        continue;
      int sx = static_cast<int>((static_cast<float>(px) - base_off_x) * inv_scale);
      sx = std::clamp(sx, 0, last_sx);
      const int local_col = flip_coordinate(sx, last_sx, item.flip_x);
      const int src_col = item.src.x + local_col;
      const std::size_t src_index = (src_row_base + static_cast<std::size_t>(src_col)) * 4;
      const std::size_t dst_index = (dst_row_base + static_cast<std::size_t>(px)) * 4;
      blend_pixel(pixels, image.pixels, src_index, dst_index);
    }
  }
}

/// Each quad vertex from stb_easy_font: x, y, z (float) + color (4 bytes).
struct EasyFontVertex {
  float x{};
  float y{};
  float z{};
  std::array<std::uint8_t, 4> col{};
};

void fill_quads(std::vector<uint8_t> &pixels, int out_w, int out_h, int ox, int oy, const std::string &text,
                std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  constexpr std::size_t k_max_quads = 512;
  std::array<EasyFontVertex, k_max_quads * 4> vbuf{};
  // stb_easy_font_print takes a non-const char*, though it only reads the text.
  std::string mutable_text = text;
  const int num_quads = stb_easy_font_print(static_cast<float>(ox), static_cast<float>(oy), mutable_text.data(),
                                            nullptr, vbuf.data(), static_cast<int>(sizeof(vbuf)));
  for (int q = 0; q < num_quads; ++q) {
    const EasyFontVertex *v = &vbuf[static_cast<std::size_t>(q) * 4];
    const int x0 = static_cast<int>(std::ranges::min({v[0].x, v[1].x, v[2].x, v[3].x}));
    const int y0 = static_cast<int>(std::ranges::min({v[0].y, v[1].y, v[2].y, v[3].y}));
    const int x1 = static_cast<int>(std::ranges::max({v[0].x, v[1].x, v[2].x, v[3].x}));
    const int y1 = static_cast<int>(std::ranges::max({v[0].y, v[1].y, v[2].y, v[3].y}));
    for (int py = y0; py < y1; ++py) {
      if (py < 0 || py >= out_h)
        continue;
      const std::size_t row_base = static_cast<std::size_t>(py) * static_cast<std::size_t>(out_w);
      for (int px = x0; px < x1; ++px) {
        if (px < 0 || px >= out_w)
          continue;
        const std::size_t idx = (row_base + static_cast<std::size_t>(px)) * 4;
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

/// The world manifest's chunk layout.
struct WorldLayout {
  int chunks_wide{0};

  int chunks_tall{0};

  int chunk_size{0};
};

/// One chunk label's chunk coordinates.
struct ChunkLabel {
  int chunk_col{0};

  int chunk_row{0};
};

/// Everything one world render needs: loaded atlases, draw items, labels and world-pixel bounds.
struct CollectedDraw {
  std::unordered_map<std::string, int> slot_by_path{};

  std::vector<AtlasImage> images{};

  std::vector<DrawItem> depth_items{};

  std::vector<DrawItem> above_items{};

  std::vector<ChunkLabel> labels{};

  float min_x{std::numeric_limits<float>::max()};

  float min_y{std::numeric_limits<float>::max()};

  float max_x{std::numeric_limits<float>::lowest()};

  float max_y{std::numeric_limits<float>::lowest()};
};

/// Read and announce the world manifest's layout; nullopt when it can't be opened or parsed.
std::optional<WorldLayout> load_world_layout(const fs::path &world_dir) {
  const fs::path manifest_path = world_dir / "manifest.json";
  std::ifstream manifest_file(manifest_path);
  if (!manifest_file) {
    std::println(stderr, "Cannot open: {}", manifest_path.string());
    return std::nullopt;
  }
  const json manifest = json::parse(manifest_file);
  const WorldLayout layout{
      .chunks_wide = manifest.at("chunks_wide").get<int>(),
      .chunks_tall = manifest.at("chunks_tall").get<int>(),
      .chunk_size = manifest.at("chunk_size").get<int>(),
  };
  std::println("World: {}×{} chunks, {} tiles/chunk", layout.chunks_wide, layout.chunks_tall, layout.chunk_size);
  return layout;
}

/// Isometric projection shared by every chunk, anchored on chunk_0_0's diamond.
std::optional<IsometricParams> probe_projection(const fs::path &world_dir, const WorldLayout &layout, float scale) {
  const fs::path probe = world_dir / std::format("chunk_{}_{}.json", 0, 0);
  const auto tilemap = load_tilemap(corundum::core::DirectoryContentSource{}, probe);
  if (!tilemap) {
    std::println(stderr, "Cannot load {} : {}", probe.string(), tilemap.error());
    return std::nullopt;
  }
  return compute_isometric_params(tilemap->diamond_w(), tilemap->diamond_h(), layout.chunks_tall * layout.chunk_size,
                                  scale, /*elev_step=*/4.f);
}

void track_bounds(CollectedDraw &draw, const DrawItem &item) {
  draw.min_x = std::min(draw.min_x, item.pos_x);
  draw.min_y = std::min(draw.min_y, item.pos_y);
  draw.max_x = std::max(draw.max_x, item.pos_x + (static_cast<float>(item.src.width) * item.scale));
  draw.max_y = std::max(draw.max_y, item.pos_y + (static_cast<float>(item.src.height) * item.scale));
}

/// One image slot per tileset of `tm`, loading each atlas once into `draw`.
std::vector<int> chunk_image_slots(const Tilemap &tm, CollectedDraw &draw) {
  std::vector<int> image_slots;
  image_slots.reserve(tm.tilesets.size());
  for (const TilemapTileset &ts : tm.tilesets)
    image_slots.push_back(load_image(draw.slot_by_path, draw.images, ts.info.path).value_or(-1));
  return image_slots;
}

/// Resolve one cell and, when it draws, file it into `draw` and widen the bounds.
void collect_cell(const Tilemap &tm, const TilemapLayer &layer, int col, int row, int world_col, int world_row,
                  const IsometricParams &iso, float scale, const std::vector<int> &image_slots, CollectedDraw &draw) {
  const auto item = resolve_tile(tm, layer, iso, col, row, world_col, world_row, scale, image_slots);
  if (!item)
    return;
  track_bounds(draw, *item);
  if (item->above)
    draw.above_items.push_back(*item);
  else
    draw.depth_items.push_back(*item);
}

/// Resolve every cell of `tm` into `draw`'s depth-sorted or above lists.
void collect_chunk(const Tilemap &tm, int chunk_col, int chunk_row, const WorldLayout &layout,
                   const IsometricParams &iso, float scale, CollectedDraw &draw) {
  const std::vector<int> image_slots = chunk_image_slots(tm, draw);
  for (const TilemapLayer &layer : tm.layers) {
    for (int row = 0; row < tm.height; ++row) {
      for (int col = 0; col < tm.width; ++col) {
        const int world_col = (chunk_col * layout.chunk_size) + col;
        const int world_row = (chunk_row * layout.chunk_size) + row;
        collect_cell(tm, layer, col, row, world_col, world_row, iso, scale, image_slots, draw);
      }
    }
  }
}

/// Load and resolve every chunk. Unreadable chunks are warned about and skipped.
void collect_world_draws(const fs::path &world_dir, const WorldLayout &layout, const IsometricParams &iso, float scale,
                         bool labels, CollectedDraw &draw) {
  const corundum::core::DirectoryContentSource content{};
  corundum::world::tilemap::TilemapLoader chunk_loader{content};
  int rendered{};
  for (int cy = 0; cy < layout.chunks_tall; ++cy) {
    for (int cx = 0; cx < layout.chunks_wide; ++cx) {
      const fs::path chunk_path = world_dir / std::format("chunk_{}_{}.json", cx, cy);
      const auto tilemap = chunk_loader.load_tilemap(chunk_path);
      if (!tilemap) {
        std::println(stderr, "Warning: cannot load {} : {}", chunk_path.string(), tilemap.error());
        continue;
      }
      collect_chunk(*tilemap, cx, cy, layout, iso, scale, draw);
      if (labels)
        draw.labels.push_back({.chunk_col = cx, .chunk_row = cy});
      ++rendered;
      if (rendered % 32 == 0)
        std::println("  {}/{} chunks...", rendered, layout.chunks_wide * layout.chunks_tall);
    }
  }
}

/// Draw the collected items into a PNG; false when nothing was collected or the write fails.
bool write_map_png(const std::string &output_path, const WorldLayout &layout, const IsometricParams &iso,
                   CollectedDraw &draw) {
  constexpr int k_margin = 4;
  const float offset_x = static_cast<float>(k_margin) - draw.min_x;
  const float offset_y = static_cast<float>(k_margin) - draw.min_y;
  const int out_w = static_cast<int>(std::ceil(draw.max_x - draw.min_x)) + (2 * k_margin);
  const int out_h = static_cast<int>(std::ceil(draw.max_y - draw.min_y)) + (2 * k_margin);
  std::println("Output image: {}×{} px", out_w, out_h);
  if (out_w <= 0 || out_h <= 0) {
    std::println(stderr, "Nothing to render (no tiles found)");
    return false;
  }

  std::vector<uint8_t> pixels(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0u);
  std::ranges::stable_sort(draw.depth_items, {}, &DrawItem::depth);
  for (const DrawItem &item : draw.depth_items)
    draw_item(pixels, out_w, out_h, draw.images, offset_x, offset_y, item);
  for (const DrawItem &item : draw.above_items)
    draw_item(pixels, out_w, out_h, draw.images, offset_x, offset_y, item);

  for (const ChunkLabel &label : draw.labels) {
    const Vec2 base = tile_to_world(static_cast<float>(label.chunk_col * layout.chunk_size),
                                    static_cast<float>(label.chunk_row * layout.chunk_size), 0, iso);
    draw_label(pixels, out_w, out_h, static_cast<int>(base.x + offset_x), static_cast<int>(base.y + offset_y),
               std::format("chunk_{}_{}", label.chunk_col, label.chunk_row));
  }

  std::println("Writing {}...", output_path);
  if (stbi_write_png(output_path.c_str(), out_w, out_h, 4, pixels.data(), out_w * 4) == 0) {
    std::println(stderr, "Failed to write PNG: {}", output_path);
    return false;
  }
  std::println("Done. {}×{} px written to: {}", out_w, out_h, output_path);
  return true;
}

/// Parse arguments, build the world render and write it; returns the process exit code.
int run(int argc, const char *const *argv) {
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
  const auto layout = load_world_layout(world_dir);
  if (!layout)
    return 1;
  const auto iso = probe_projection(world_dir, *layout, args.scale);
  if (!iso)
    return 1;

  CollectedDraw draw;
  collect_world_draws(world_dir, *layout, *iso, args.scale, args.labels, draw);
  return write_map_png(args.output, *layout, *iso, draw) ? 0 : 1;
}

} // namespace

} // namespace tools::mapview

int main(int argc, char *const argv[]) {
  try {
    return tools::mapview::run(argc, argv);
  } catch (const std::exception &error) {
    std::fputs("Error: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
  }
}
