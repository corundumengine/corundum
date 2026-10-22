// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/texture_ref_counts.hpp>
#include <corundum/render/area.hpp>
#include <corundum/render/render_state.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "memory_content_source.hpp"
#include "render_state_fixtures.hpp"
#include "world_transition_fixtures.hpp"

using corundum::test::disk;

namespace {

  using corundum::core::GameConfig;
  using corundum::platform::DrawLine;
  using corundum::platform::DrawRect;
  using corundum::platform::DrawSprite;
  using corundum::platform::DrawText;
  using corundum::platform::RendererStats;
  using corundum::render::RenderMode;
  using corundum::render::RenderState;

  /// Renderer that hands out a distinct id per path and tracks references like SokolRenderer.
  class CountingRenderer final : public corundum::platform::Renderer {
  public:
    corundum::platform::TextureRefCounts refs;
    std::vector<uint32_t> purged;

    std::expected<uint32_t, std::string> load_texture(std::string_view path) override {
      const auto [it, inserted] = ids_.try_emplace(std::string{path}, next_id_);
      if (inserted)
        ++next_id_;
      refs.acquire(it->second);
      return it->second;
    }

    [[nodiscard]] corundum::core::math::Vec2 texture_size(uint32_t /*texture_id*/) const override { return {}; }

    void release_texture(uint32_t texture_id) override { refs.release(texture_id); }

    void purge_unused_textures() override {
      for (const uint32_t id : refs.take_unreferenced()) {
        purged.push_back(id);
        std::erase_if(ids_, [id](const auto &entry) { return entry.second == id; });
      }
    }

    std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override { return 1u; }

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                        float /*zoom*/) override {}

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override { return true; }

    void end_frame() override {}

    void draw(const DrawSprite & /*cmd*/) override {}

    void draw(const DrawText & /*cmd*/) override {}

    void draw(const DrawRect & /*cmd*/) override {}

    void draw(const DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view /*text*/,
                                     uint32_t /*char_size*/) const override {
      return 0.f;
    }

    [[nodiscard]] RendererStats stats() const override { return {}; }

  private:
    std::unordered_map<std::string, uint32_t> ids_;
    uint32_t next_id_{1};
  };

  GameConfig fixture_config() {
    return corundum::test::make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
  }

  /// Sorted copy, so set-equality checks read clearly and failures show both orderings.
  std::vector<uint32_t> sorted(std::vector<uint32_t> ids) {
    std::ranges::sort(ids);
    return ids;
  }

} // namespace

TEST_CASE("area textures: a loaded world holds one reference per distinct tileset texture") {
  CountingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_world(disk(), renderer, state, fixture_config(), {}).has_value());

  REQUIRE_FALSE(state.area_texture_ids.empty());
  for (const uint32_t id : state.area_texture_ids) {
    INFO(id);
    CHECK_EQ(renderer.refs.count(id), 1u);
  }
}

TEST_CASE("area textures: unload_world releases and purges every area texture") {
  CountingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_world(disk(), renderer, state, fixture_config(), {}).has_value());
  const std::vector<uint32_t> loaded = state.area_texture_ids;

  corundum::render::unload_world(renderer, state);

  CHECK_EQ(sorted(renderer.purged), sorted(loaded));
  CHECK(state.area_texture_ids.empty());
  CHECK(corundum::render::mode(state) == RenderMode::None);
}

TEST_CASE("area textures: reloading the same world keeps its textures resident") {
  CountingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_world(disk(), renderer, state, fixture_config(), {}).has_value());
  REQUIRE(corundum::render::load_world(disk(), renderer, state, fixture_config(), {}).has_value());

  CHECK(renderer.purged.empty());
  for (const uint32_t id : state.area_texture_ids) {
    INFO(id);
    CHECK_EQ(renderer.refs.count(id), 1u);
  }
}

TEST_CASE("area textures: a texture also held outside the area survives unload_world") {
  CountingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_world(disk(), renderer, state, fixture_config(), {}).has_value());
  const std::string path = corundum::test::world_area_of(state).chunks.active_at(0).tilemap.tilesets[0].info.path;
  const auto id = renderer.load_texture(path);
  REQUIRE(id.has_value());

  corundum::render::unload_world(renderer, state);

  CHECK(std::ranges::find(renderer.purged, *id) == renderer.purged.end());
  CHECK_EQ(renderer.refs.count(*id), 1u);
}

TEST_CASE("area textures: a process-lifetime texture loaded before the area survives unload_world") {
  CountingRenderer renderer;
  const auto process_id = renderer.load_texture("ui/border.png");
  REQUIRE(process_id.has_value());
  RenderState state;
  REQUIRE(corundum::render::load_world(disk(), renderer, state, fixture_config(), {}).has_value());

  corundum::render::unload_world(renderer, state);

  CHECK(std::ranges::find(renderer.purged, *process_id) == renderer.purged.end());
  CHECK_EQ(renderer.refs.count(*process_id), 1u);
}
