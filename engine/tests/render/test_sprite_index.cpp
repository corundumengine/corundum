// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/render_state.hpp>
#include <corundum/render/resources.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/sprites/sprite.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>

#include "memory_content_source.hpp"
#include "recording_renderer.hpp"

using corundum::test::disk;

namespace {

  using corundum::sprites::AnimId;
  using corundum::sprites::k_num_anim_ids;
  using corundum::test::TileRecordingRenderer;

  corundum::sprites::CharacterRegistry load_fixture_registry() {
    corundum::sprites::CharacterRegistry registry;
    REQUIRE(
        registry.load_all(disk(), std::filesystem::path{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR} / "sprites").has_value());
    return registry;
  }

  /// Calls @p check for every non-empty animation frame of every sprite.
  template <typename Check>
  void for_each_animated_frame(const corundum::sprites::CharacterRegistry &registry, const Check &check) {
    for (const auto &[name, frame] : registry.frames()) {
      INFO(name);
      for (uint8_t anim = 0; anim < k_num_anim_ids; ++anim) {
        if (frame.anim_frames[anim].empty())
          continue;
        INFO(anim);
        check(frame, static_cast<AnimId>(anim));
      }
    }
  }

} // namespace

TEST_CASE("load_sprite_index: every animated frame resolves to its sheet's texture") {
  const corundum::sprites::CharacterRegistry registry = load_fixture_registry();
  TileRecordingRenderer renderer;
  corundum::render::RenderState state;
  corundum::render::load_sprite_index(renderer, state, registry);

  for_each_animated_frame(registry, [&](const corundum::sprites::Frames &frame, AnimId anim) {
    const corundum::sprites::SpriteSheet *sheet = registry.get_sheet(frame.sheet_id);
    REQUIRE(sheet != nullptr);
    const uint32_t expected = renderer.ids_by_path.at(sheet->path);

    const auto entry = state.sprite_index.get(frame.sprite_id, anim, 0);
    REQUIRE(entry.has_value());
    CHECK_EQ(entry->tex_id, expected);
  });
}

TEST_CASE("load_sprite_index: a sheet whose texture fails to load leaves its frames unresolved") {
  const corundum::sprites::CharacterRegistry registry = load_fixture_registry();
  TileRecordingRenderer renderer;
  for (const auto &[sheet_id, sheet] : registry.sheets()) {
    INFO(sheet_id);
    renderer.failing_paths.push_back(sheet.path);
  }
  corundum::render::RenderState state;
  corundum::render::load_sprite_index(renderer, state, registry);

  for_each_animated_frame(registry, [&](const corundum::sprites::Frames &frame, AnimId anim) {
    CHECK_FALSE(state.sprite_index.get(frame.sprite_id, anim, 0).has_value());
  });
}
