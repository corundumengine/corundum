// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

  corundum::gameplay::dialogue::Graph make_talk_graph(std::string text) {
    using namespace corundum::gameplay::dialogue;
    Graph g;
    g.graph_id = "reveal_test";
    g.speaker = "NPC";

    Node first;
    first.id = "n0";
    first.type = NodeType::Talk;
    first.text = std::move(text);
    first.next_id = "n1";
    g.id_to_index[first.id] = g.nodes.size();
    g.nodes.push_back(std::move(first));

    Node second;
    second.id = "n1";
    second.type = NodeType::Talk;
    second.text = "Second";
    g.id_to_index[second.id] = g.nodes.size();
    g.nodes.push_back(std::move(second));
    return g;
  }

  bool contains_text(const corundum::test::RecordingRenderer &r, std::string_view needle) {
    for (const auto &call : r.log) {
      if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr && text->text == needle)
        return true;
    }
    return false;
  }

  const corundum::platform::DrawText *find_text(const corundum::test::RecordingRenderer &r, std::string_view needle) {
    for (const auto &call : r.log) {
      if (const auto *text = std::get_if<corundum::platform::DrawText>(&call); text != nullptr && text->text == needle)
        return text;
    }
    return nullptr;
  }

  corundum::ui::FontFamily dialogue_family() {
    return corundum::ui::FontFamily{.ids = {10u, 11u, 12u, 13u}};
  }

} // namespace

TEST_CASE("dialog reveal: a zero rate draws the full body immediately") {
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph("Hello world");
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  corundum::gameplay::screens::DialogBoxState box{};
  const corundum::ui::PanelSkin skin{};
  const float text_speed = 0.f;

  corundum::test::RecordingRenderer r;
  corundum::gameplay::screens::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f}, skin, text_speed);
  corundum::gameplay::screens::dialog_box_render(box, r, skin);

  CHECK(contains_text(r, "Hello world"));
}

TEST_CASE("dialog reveal: advance reveals a codepoint prefix of the body") {
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph("Hello world");
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  corundum::gameplay::screens::DialogBoxState box{};
  const corundum::ui::PanelSkin skin{};
  const float text_speed = 10.f / corundum::gameplay::screens::k_base_reveal_chars_per_second;

  corundum::test::RecordingRenderer r;
  corundum::gameplay::screens::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f}, skin, text_speed);
  REQUIRE(box.reveal_chars == doctest::Approx(0.f));

  corundum::gameplay::screens::dialog_box_advance(box, conversation, 0.2f, text_speed);
  corundum::gameplay::screens::dialog_box_advance(box, conversation, 0.3f, text_speed);
  REQUIRE(box.reveal_chars == doctest::Approx(5.f));

  corundum::gameplay::screens::dialog_box_render(box, r, skin);
  CHECK(contains_text(r, "Hello"));
  CHECK_FALSE(contains_text(r, "Hello world"));
}

TEST_CASE("dialog reveal: switching node resets the reveal") {
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph("Hello world");
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  corundum::gameplay::screens::DialogBoxState box{};
  const corundum::ui::PanelSkin skin{};
  const float text_speed = 10.f / corundum::gameplay::screens::k_base_reveal_chars_per_second;

  corundum::test::RecordingRenderer r;
  corundum::gameplay::screens::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f}, skin, text_speed);
  corundum::gameplay::screens::dialog_box_advance(box, conversation, 1.f, text_speed);
  REQUIRE(box.reveal_chars > 0.f);

  // A stale reveal marker simulates the conversation having moved to a new node.
  box.reveal_node_id = "some_other_node";
  corundum::gameplay::screens::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f}, skin, text_speed);
  CHECK(box.reveal_chars == doctest::Approx(0.f));
}

TEST_CASE("dialog reveal: a budget spanning a style boundary reveals each segment with its own font") {
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph("a *b* c");
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  corundum::gameplay::screens::DialogBoxState box{};
  corundum::ui::PanelSkin skin{};
  skin.style.fonts[static_cast<std::size_t>(corundum::ui::FontRole::Dialogue)] = dialogue_family();
  const float text_speed = 10.f / corundum::gameplay::screens::k_base_reveal_chars_per_second;

  corundum::test::RecordingRenderer r;
  corundum::gameplay::screens::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f}, skin, text_speed);
  corundum::gameplay::screens::dialog_box_advance(box, conversation, 0.3f, text_speed); // 3 codepoints
  corundum::gameplay::screens::dialog_box_render(box, r, skin);

  const corundum::platform::DrawText *regular = find_text(r, "a ");
  const corundum::platform::DrawText *italic = find_text(r, "b");
  REQUIRE(regular != nullptr);
  REQUIRE(italic != nullptr);
  CHECK(regular->font_id == 10u);
  CHECK(italic->font_id == 12u);
  CHECK(italic->position.x > regular->position.x); // the italic run continues the line, not restarts it
  CHECK_FALSE(contains_text(r, " c"));
}

TEST_CASE("dialog reveal: the full styled body draws each run with its own font and keeps the markup out of the text") {
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph("bold **b** and italic *i*");
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  corundum::gameplay::screens::DialogBoxState box{};
  corundum::ui::PanelSkin skin{};
  skin.style.fonts[static_cast<std::size_t>(corundum::ui::FontRole::Dialogue)] = dialogue_family();

  corundum::test::RecordingRenderer r;
  corundum::gameplay::screens::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f}, skin, 0.f);
  corundum::gameplay::screens::dialog_box_render(box, r, skin);

  const corundum::platform::DrawText *bold = find_text(r, "b");
  const corundum::platform::DrawText *italic = find_text(r, "i");
  REQUIRE(bold != nullptr);
  REQUIRE(italic != nullptr);
  CHECK(bold->font_id == 11u);
  CHECK(italic->font_id == 12u);
  CHECK_FALSE(contains_text(r, "**b**"));
  CHECK_FALSE(contains_text(r, "*i*"));
}
