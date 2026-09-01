#pragma once

#include <algorithm>
#include <corundum/world/tilemap/tilemap.hpp>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace tools::tilemap {

  enum class LayerPreset { Ground, FloorDetail, Water, Walls, Roof, Decor, Blank };

  /** @brief Display string for a preset in the "+" popup; also its canonical layer name.
   *
   * Not meaningful for Blank (Blank's name is generated, not fixed).
   *
   * @param preset The preset to label.
   * @return Canonical name; "Blank" for @p LayerPreset::Blank or any unhandled value.
   */
  [[nodiscard]] inline std::string_view layer_preset_label(LayerPreset preset) {
    switch (preset) {
      using enum LayerPreset;
      case Ground:
        return "Ground";
      case FloorDetail:
        return "Floor Detail";
      case Water:
        return "Water";
      case Walls:
        return "Walls";
      case Roof:
        return "Roof";
      case Decor:
        return "Decor";
      default:
        return "Blank";
    }
  }

  /** @brief Check whether any layer in @p existing_layers already has the given name.
   *
   * @param name Name to test (case-sensitive, exact match).
   * @param existing_layers Layers to search.
   * @return true if any layer's name equals @p name.
   */
  [[nodiscard]] inline bool layer_name_taken(std::string_view name,
                                             std::span<const corundum::world::tilemap::TilemapLayer> existing_layers) {
    return std::ranges::contains(existing_layers, name, &corundum::world::tilemap::TilemapLayer::name);
  }

  /** @brief Pick the first free layer name based on @p base.
   *
   * Returns @p base unchanged if free, else `"{base} 2"`, `"{base} 3"`, ... — first free wins.
   *
   * @param base Preferred name.
   * @param existing_layers Layers to search for collisions.
   * @return A name that does not collide with any layer in @p existing_layers.
   */
  [[nodiscard]] inline std::string
  dedup_layer_name(std::string_view base, std::span<const corundum::world::tilemap::TilemapLayer> existing_layers) {
    if (!layer_name_taken(base, existing_layers))
      return std::string(base);
    for (int n = 2;; ++n) {
      std::string candidate = std::format("{} {}", base, n);
      if (!layer_name_taken(candidate, existing_layers))
        return candidate;
    }
  }

  /** @brief Build a new layer configured for @p preset.
   *
   * Sized width*height and filled with k_empty_tile. name/z_index/depth_sorted are set per the
   * convention table in docs/building-tilemaps.md's "Layers" section.
   *
   * Blank reproduces the pre-preset default: `"Layer {N}"`, z_index=0, depth_sorted=false,
   * N = existing_layers.size() + 1 (not deduped — matches the old add_layer behavior).
   *
   * @param preset          Role preset for the new layer.
   * @param width           Map width in tiles (per the owning Tilemap).
   * @param height          Map height in tiles (per the owning Tilemap).
   * @param existing_layers Layers already on the map, used for name-deduplication.
   * @return A fully-initialized TilemapLayer; tiles are all k_empty_tile and visible=true.
   */
  [[nodiscard]] inline corundum::world::tilemap::TilemapLayer
  make_layer_from_preset(LayerPreset preset, int width, int height,
                         std::span<const corundum::world::tilemap::TilemapLayer> existing_layers) {
    using corundum::world::tilemap::k_empty_tile;
    using corundum::world::tilemap::TilemapLayer;

    TilemapLayer layer;
    switch (preset) {
      case LayerPreset::Ground:
        layer.name = dedup_layer_name("Ground", existing_layers);
        layer.z_index = 0;
        layer.depth_sorted = false;
        break;
      case LayerPreset::FloorDetail:
        layer.name = dedup_layer_name("Floor Detail", existing_layers);
        layer.z_index = 0;
        layer.depth_sorted = false;
        break;
      case LayerPreset::Water:
        layer.name = dedup_layer_name("Water", existing_layers);
        layer.z_index = 0;
        layer.depth_sorted = false;
        break;
      case LayerPreset::Walls:
        layer.name = dedup_layer_name("Walls", existing_layers);
        layer.z_index = 1;
        layer.depth_sorted = true;
        break;
      case LayerPreset::Roof:
        layer.name = dedup_layer_name("Roof", existing_layers);
        layer.z_index = 1;
        layer.depth_sorted = false;
        break;
      case LayerPreset::Decor:
        layer.name = dedup_layer_name("Decor", existing_layers);
        layer.z_index = 2;
        layer.depth_sorted = false;
        break;
      case LayerPreset::Blank:
        layer.name = std::format("Layer {}", existing_layers.size() + 1);
        layer.z_index = 0;
        layer.depth_sorted = false;
        break;
    }
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), k_empty_tile);
    return layer;
  }

} // namespace tools::tilemap
