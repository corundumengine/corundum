// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/world/ui_stack.hpp>

#include "ui/recording_renderer.hpp"
#include "world_transition_fixtures.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_border;
  using corundum::test::make_world_config;
  using corundum::test::RecordingRenderer;
  namespace screens = corundum::gameplay::screens;

  corundum::gameplay::dialogue::Graph make_talk_graph(std::string text) {
    using namespace corundum::gameplay::dialogue;
    Graph graph;
    graph.graph_id = "modal_top_draw";
    graph.speaker = "NPC";

    Node node;
    node.id = "n0";
    node.type = NodeType::Talk;
    node.text = std::move(text);
    node.next_id = "end";
    graph.id_to_index[node.id] = graph.nodes.size();
    graph.nodes.push_back(std::move(node));
    return graph;
  }

  bool contains_text(const RecordingRenderer &r, std::string_view needle) {
    for (const auto &call : r.log) {
      if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr && text->text == needle)
        return true;
    }
    return false;
  }

  void init_engine(corundum::Engine &engine) {
    adopt_platform(engine, 320, 240);
    const std::filesystem::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
    REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  }

} // namespace

TEST_CASE("modal draw: the dialogue box hides under a pushed screen and resumes intact") {
  corundum::Engine engine{};
  init_engine(engine);
  corundum::gameplay::Gameplay gameplay{engine};

  const auto graph = make_talk_graph("Hello world");
  gameplay.dialogue.emplace(graph, engine.flags);
  REQUIRE(gameplay.dialogue->is_active());

  engine.render.panel_skin.border = make_border();
  engine.render.panel_skin.style.font_id = 2;
  engine.render.text_speed = 0.f; // reveal instantly so the full body is drawn

  const corundum::core::math::Vec2 viewport{.x = 320.f, .y = 240.f};
  RecordingRenderer r;

  // Dialogue is the top mode: the Modal hook draws the box.
  engine.scene.ui.push(screens::Dialogue);
  engine.screens.render_layer(corundum::RenderLayer::Modal, engine, r, viewport);
  CHECK(contains_text(r, "Hello world"));

  // Give the box non-default reveal progress, then push a screen over it.
  gameplay.dialog_box.reveal_chars = 3.f;
  engine.scene.ui.push(corundum::world::GameMode::Menu);

  r.log.clear();
  engine.screens.render_layer(corundum::RenderLayer::Modal, engine, r, viewport);
  CHECK_FALSE(contains_text(r, "Hello world"));
  CHECK_FALSE(screens::dialog_box_visible(gameplay.dialog_box));
  // Hiding clears visibility only — layout and reveal progress stay cached for a resume.
  CHECK(gameplay.dialog_box.reveal_chars == doctest::Approx(3.f));
  REQUIRE(gameplay.dialog_box.layout.has_value());

  // Popping the menu restores the box exactly.
  engine.scene.ui.pop();
  r.log.clear();
  engine.screens.render_layer(corundum::RenderLayer::Modal, engine, r, viewport);
  CHECK(contains_text(r, "Hello world"));
  CHECK(gameplay.dialog_box.reveal_chars == doctest::Approx(3.f));

  engine.cleanup();
}
