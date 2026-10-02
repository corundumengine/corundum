// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/dialogue/conversation.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/world/flags.hpp>

#include "ui/recording_renderer.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

  corundum::dialogue::Graph make_talk_graph(std::string text) {
    using namespace corundum::dialogue;
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

} // namespace

TEST_CASE("dialog reveal: a zero rate draws the full body immediately") {
  corundum::world::FlagStore flags;
  const corundum::dialogue::Graph graph = make_talk_graph("Hello world");
  const corundum::dialogue::Conversation conversation{graph, flags};

  corundum::ui::DialogBoxState box{};
  box.style.font_id = 2;
  box.reveal_chars_per_second = 0.f;

  corundum::test::RecordingRenderer r;
  corundum::ui::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f});
  corundum::ui::dialog_box_render(box, r);

  CHECK(contains_text(r, "Hello world"));
}

TEST_CASE("dialog reveal: advance reveals a codepoint prefix of the body") {
  corundum::world::FlagStore flags;
  const corundum::dialogue::Graph graph = make_talk_graph("Hello world");
  const corundum::dialogue::Conversation conversation{graph, flags};

  corundum::ui::DialogBoxState box{};
  box.style.font_id = 2;
  box.reveal_chars_per_second = 10.f;

  corundum::test::RecordingRenderer r;
  corundum::ui::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f});
  REQUIRE(box.reveal_chars == doctest::Approx(0.f));

  corundum::ui::dialog_box_advance(box, conversation, 0.2f);
  corundum::ui::dialog_box_advance(box, conversation, 0.3f);
  REQUIRE(box.reveal_chars == doctest::Approx(5.f));

  corundum::ui::dialog_box_render(box, r);
  CHECK(contains_text(r, "Hello"));
  CHECK_FALSE(contains_text(r, "Hello world"));
}

TEST_CASE("dialog reveal: switching node resets the reveal") {
  corundum::world::FlagStore flags;
  const corundum::dialogue::Graph graph = make_talk_graph("Hello world");
  const corundum::dialogue::Conversation conversation{graph, flags};

  corundum::ui::DialogBoxState box{};
  box.style.font_id = 2;
  box.reveal_chars_per_second = 10.f;

  corundum::test::RecordingRenderer r;
  corundum::ui::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f});
  corundum::ui::dialog_box_advance(box, conversation, 1.f);
  REQUIRE(box.reveal_chars > 0.f);

  // A stale reveal marker simulates the conversation having moved to a new node.
  box.reveal_node_id = "some_other_node";
  corundum::ui::dialog_box_update(box, conversation, r, {.x = 800.f, .y = 600.f});
  CHECK(box.reveal_chars == doctest::Approx(0.f));
}
