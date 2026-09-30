// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/null/null_renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>

#include "temp_dir.hpp"
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_ui_assets_", tag};
  }

  /// A 6×6-frame atlas whose nine-patch cells are 2×2 frames (32px), matching the shipped
  /// border. The loader must report the cell size (span × frame), not the single-frame size.
  constexpr std::string_view k_borders_json = R"({
    "frame_width": 16,
    "frame_height": 16,
    "path": "assets/textures/ui/border/panel.png",
    "frames": {
      "upper_left":  { "col": 0, "row": 0, "col_span": 2, "row_span": 2 },
      "top":         { "col": 2, "row": 0, "col_span": 2, "row_span": 2 },
      "upper_right": { "col": 4, "row": 0, "col_span": 2, "row_span": 2 },
      "left":        { "col": 0, "row": 2, "col_span": 2, "row_span": 2 },
      "center":      { "col": 2, "row": 2, "col_span": 2, "row_span": 2 },
      "right":       { "col": 4, "row": 2, "col_span": 2, "row_span": 2 },
      "lower_left":  { "col": 0, "row": 4, "col_span": 2, "row_span": 2 },
      "bottom":      { "col": 2, "row": 4, "col_span": 2, "row_span": 2 },
      "lower_right": { "col": 4, "row": 4, "col_span": 2, "row_span": 2 }
    }
  })";

} // namespace

TEST_CASE("load_ui_assets — border cell size comes from the upper_left frame's span") {
  const auto dir = temp_dir("cell_span");
  const fs::path manifest = dir / "borders.json";
  write_file(manifest, k_borders_json);

  corundum::platform::null::NullRenderer renderer;
  corundum::render::RenderState state;
  const auto result = corundum::render::load_ui_assets(renderer, state, manifest.string());

  REQUIRE(result.has_value());
  CHECK(state.dialog_box.border.texture_id == corundum::platform::null::k_dummy_handle);
  // 2×2 frames of 16px = a 32px cell; the nine-patch derives the 96px grid from this.
  CHECK(state.dialog_box.border.tile_w == 32);
  CHECK(state.dialog_box.border.tile_h == 32);
}

TEST_CASE("load_ui_assets — a missing manifest is a non-fatal no-op") {
  corundum::platform::null::NullRenderer renderer;
  corundum::render::RenderState state;
  const auto result = corundum::render::load_ui_assets(renderer, state, "/nonexistent/borders.json");

  CHECK(result.has_value());
  CHECK(state.dialog_box.border.texture_id == 0);
}
