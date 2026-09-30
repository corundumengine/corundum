// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/flags.hpp>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>

#include <corundum/dialogue/conversation.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/item/item.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/dialog_layout.hpp>
#include <corundum/ui/inventory_panel.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/prompt_box.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <deque>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

  using corundum::platform::DrawRect;
  using corundum::platform::DrawSprite;
  using corundum::platform::DrawText;

  /// Records every draw call into one ordered log so tests can assert both
  /// counts and ordering. measure_text mirrors the null backend's per-glyph
  /// width so tests don't depend on real font metrics.
  class RecordingRenderer final : public corundum::platform::Renderer {
  public:
    using DrawCall = std::variant<DrawRect, DrawSprite, DrawText>;
    std::vector<DrawCall> log{};
    // DrawText::text is a string_view that may point into temporaries that die
    // when the render call returns (e.g. formatted inventory labels); the real
    // renderer consumes it synchronously, but this recorder must keep it alive.
    // A deque (not vector) keeps references to stored strings stable across
    // push_back, since the recorded DrawText views point into this container.
    std::deque<std::string> text_owner{};

    std::expected<uint32_t, std::string> load_texture(std::string_view /*path*/) override {
      return 1u;
    }

    std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override {
      return 2u;
    }

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                        float /*zoom*/) override {}

    void reset_screen_view() override {}

    bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override {
      return true;
    }

    void end_frame() override {}

    void draw(const DrawSprite &cmd) override {
      log.emplace_back(cmd);
    }

    void draw(const DrawText &cmd) override {
      text_owner.emplace_back(cmd.text);
      DrawText copy = cmd;
      copy.text = text_owner.back();
      log.emplace_back(copy);
    }

    void draw(const DrawRect &cmd) override {
      log.emplace_back(cmd);
    }

    void draw(const corundum::platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view text,
                                     uint32_t /*char_size*/) const override {
      return static_cast<float>(text.size()) * 8.f;
    }

    [[nodiscard]] corundum::platform::RendererStats stats() const override {
      return {};
    }
  };

  corundum::ui::NinePatchBorder make_border() {
    corundum::ui::NinePatchBorder b{};
    b.texture_id = 1u;
    b.tile_w = 4;
    b.tile_h = 4;
    return b;
  }

} // namespace

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("ui_draw: panel_chrome emits exactly one DrawRect then the border's sprite commands") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::core::math::Colour bg{.r = 20, .g = 20, .b = 20, .a = 200};
  const corundum::core::math::Vec2 pos{.x = 10.f, .y = 20.f};
  const corundum::core::math::Vec2 size{.x = 100.f, .y = 60.f};

  corundum::ui::panel_chrome(r, bg, border, pos, size);

  // The border draws 8 sprites (4 corners + 2 horizontal edges + 2 vertical edges).
  REQUIRE(r.log.size() == 9);

  // First call is the fill rect, then the border's sprites — render order matters
  // because the fill must be behind the frame.
  const DrawRect &first = std::get<DrawRect>(r.log[0]);
  CHECK(first.position.x == pos.x);
  CHECK(first.position.y == pos.y);
  CHECK(first.size.x == size.x);
  CHECK(first.size.y == size.y);
  CHECK(first.colour.r == bg.r);
  CHECK(first.colour.a == bg.a);

  for (std::size_t i = 1; i < r.log.size(); ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));
}

TEST_CASE("ui_draw: panel_chrome is a no-op for the sprite half when the border has no texture") {
  RecordingRenderer r;
  corundum::ui::NinePatchBorder border{};
  border.texture_id = 0;
  border.tile_w = 4;
  border.tile_h = 4;

  corundum::ui::panel_chrome(r, {.r = 0, .g = 0, .b = 0, .a = 255}, border, {.x = 0.f, .y = 0.f},
                             {.x = 50.f, .y = 50.f});

  // The fill still goes out; only the border is skipped.
  REQUIRE(r.log.size() == 1);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("ui_draw: nine_patch_render emits corners and correctly stretched edges") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border(); // 4×4 cells
  const corundum::core::math::Vec2 pos{.x = 10.f, .y = 20.f};
  const corundum::core::math::Vec2 size{.x = 100.f, .y = 60.f};

  corundum::ui::nine_patch_render(r, border, pos.x, pos.y, size.x, size.y);

  // Order: four corners at natural size (TL, TR, BL, BR), then horizontal edges, then vertical edges.
  REQUIRE(r.log.size() == 8);
  for (const auto &call : r.log)
    REQUIRE(std::holds_alternative<DrawSprite>(call));

  const auto sprite = [&](std::size_t i) -> const DrawSprite & { return std::get<DrawSprite>(r.log[i]); };

  // Top-left corner sits at the rect origin at natural size.
  CHECK(sprite(0).source.x == 0);
  CHECK(sprite(0).source.y == 0);
  CHECK(sprite(0).source.width == 4);
  CHECK(sprite(0).source.height == 4);
  CHECK(sprite(0).position.x == pos.x);
  CHECK(sprite(0).position.y == pos.y);
  CHECK(sprite(0).scale.x == 1.f);
  CHECK(sprite(0).scale.y == 1.f);

  // Top-right corner is pinned to the rect's right/top edges.
  CHECK(sprite(1).source.x == 8);
  CHECK(sprite(1).source.y == 0);
  CHECK(sprite(1).position.x == pos.x + size.x - 4.f);
  CHECK(sprite(1).position.y == pos.y);

  // Top-middle edge stretches only on X across the inner span: (100 - 8) / 4 = 23.
  CHECK(sprite(4).source.x == 4);
  CHECK(sprite(4).source.y == 0);
  CHECK(sprite(4).position.x == pos.x + 4.f);
  CHECK(sprite(4).position.y == pos.y);
  CHECK(sprite(4).scale.x == 23.f);
  CHECK(sprite(4).scale.y == 1.f);

  // Left-middle edge stretches only on Y: (60 - 8) / 4 = 13.
  CHECK(sprite(6).source.x == 0);
  CHECK(sprite(6).source.y == 4);
  CHECK(sprite(6).position.x == pos.x);
  CHECK(sprite(6).position.y == pos.y + 4.f);
  CHECK(sprite(6).scale.x == 1.f);
  CHECK(sprite(6).scale.y == 13.f);
}

TEST_CASE("ui_draw: nine_patch_render is a no-op without a texture or with non-positive cell size") {
  RecordingRenderer r;
  corundum::ui::NinePatchBorder no_texture{};
  no_texture.tile_w = 4;
  no_texture.tile_h = 4;
  corundum::ui::nine_patch_render(r, no_texture, 0.f, 0.f, 100.f, 60.f);

  corundum::ui::NinePatchBorder zero_cell{};
  zero_cell.texture_id = 1u;
  corundum::ui::nine_patch_render(r, zero_cell, 0.f, 0.f, 100.f, 60.f);

  CHECK(r.log.empty());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("ui_draw: nine_patch_render clamps a sub-cell rect instead of emitting negative scales") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border(); // 4×4 cells

  // 4×4 is narrower/shorter than two 4px corners, so both inner spans clamp to 0.
  corundum::ui::nine_patch_render(r, border, 0.f, 0.f, 4.f, 4.f);

  REQUIRE(r.log.size() == 8);
  for (const auto &call : r.log) {
    const auto &s = std::get<DrawSprite>(call);
    CHECK(s.scale.x >= 0.f);
    CHECK(s.scale.y >= 0.f);
  }
  CHECK(std::get<DrawSprite>(r.log[4]).scale.x == 0.f); // top-middle edge
  CHECK(std::get<DrawSprite>(r.log[6]).scale.y == 0.f); // left-middle edge
}

TEST_CASE("ui_draw: draw_option emits two DrawTexts with selected colours when selected=true") {
  RecordingRenderer r;
  const corundum::ui::DialogBoxStyle style{};
  // style.selected defaults to (255, 255, 0, 255); style.choice defaults to (200, 200, 200, 255).

  const float cursor_w = corundum::ui::cursor_advance(r, style);
  corundum::ui::draw_option(r, style, "Hello", {.x = 5.f, .y = 7.f}, true);

  REQUIRE(r.log.size() == 2);
  REQUIRE(std::holds_alternative<DrawText>(r.log[0]));
  REQUIRE(std::holds_alternative<DrawText>(r.log[1]));

  // First draw: cursor prefix "> ".
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  CHECK(cursor.text == "> ");
  CHECK(cursor.position.x == 5.f);
  CHECK(cursor.position.y == 7.f);
  CHECK(cursor.colour.r == style.selected.r);
  CHECK(cursor.colour.g == style.selected.g);
  CHECK(cursor.colour.b == style.selected.b);
  // Second draw: label at pos.x + cursor_w, same colour as cursor.
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(label.text == "Hello");
  CHECK(label.position.x == 5.f + cursor_w);
  CHECK(label.position.y == 7.f);
  CHECK(label.colour.r == style.selected.r);

  CHECK(cursor_w > 0.f);
}

TEST_CASE("ui_draw: draw_option emits two DrawTexts with choice colours and two-space cursor when selected=false") {
  RecordingRenderer r;
  const corundum::ui::DialogBoxStyle style{};

  const float cursor_w = corundum::ui::cursor_advance(r, style);
  corundum::ui::draw_option(r, style, "World", {.x = 0.f, .y = 0.f}, false);

  REQUIRE(r.log.size() == 2);
  REQUIRE(std::holds_alternative<DrawText>(r.log[0]));
  REQUIRE(std::holds_alternative<DrawText>(r.log[1]));

  // Cursor is the two-space placeholder; label follows at +cursor_w.
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(cursor.text == "  ");
  CHECK(label.text == "World");
  CHECK(label.position.x == cursor_w);
  // Both writes share the unselected colour (style.choice).
  CHECK(cursor.colour.r == style.choice.r);
  CHECK(cursor.colour.g == style.choice.g);
  CHECK(cursor.colour.b == style.choice.b);
  CHECK(label.colour.r == style.choice.r);
  CHECK(label.colour.g == style.choice.g);
  CHECK(label.colour.b == style.choice.b);
}

TEST_CASE("ui_draw: draw_option aligns the label at cursor_advance whether or not it is selected") {
  // Column alignment guarantee: selected and unselected options reserve the same cursor
  // column, so the label x is pos.x + cursor_advance in both branches. If a font rendered
  // "> " and "  " at different widths this would surface here — cursor_advance always
  // measures k_choice_cursor, which is the point.
  RecordingRenderer r;
  const corundum::ui::DialogBoxStyle style{};
  const float advance = corundum::ui::cursor_advance(r, style);

  corundum::ui::draw_option(r, style, "A", {.x = 0.f, .y = 0.f}, true);
  corundum::ui::draw_option(r, style, "A", {.x = 100.f, .y = 0.f}, false);

  REQUIRE(r.log.size() == 4);
  const DrawText &selected_label = std::get<DrawText>(r.log[1]);
  const DrawText &unselected_label = std::get<DrawText>(r.log[3]);
  CHECK(selected_label.position.x == advance);
  CHECK(unselected_label.position.x == 100.f + advance);
  CHECK(advance > 0.f);
}

TEST_CASE("ui_draw: draw_option without a cursor keeps the selected colour and the hanging indent") {
  // Continuation lines of a wrapped selected option: the whole option stays highlighted
  // (style.selected) even though only the first line shows the "> " cursor.
  RecordingRenderer r;
  const corundum::ui::DialogBoxStyle style{};
  const float advance = corundum::ui::cursor_advance(r, style);

  corundum::ui::draw_option(r, style, "continuation", {.x = 3.f, .y = 9.f}, true, false);

  REQUIRE(r.log.size() == 2);
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(cursor.text == "  ");
  CHECK(label.text == "continuation");
  CHECK(label.position.x == 3.f + advance);
  CHECK(cursor.colour.r == style.selected.r);
  CHECK(label.colour.r == style.selected.r);
}

// ── prompt_box_render ───────────────────────────────────────────────────────

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("prompt_box_render: chrome, question, then the Yes/No option pairs") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::prompt_box_render(r, style, border, "Enter?", true, viewport);

  // Chrome (1 DrawRect + 8 DrawSprite), question, then Yes (cursor+label) and No (cursor+label).
  REQUIRE(r.log.size() == 9 + 1 + 4);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
  for (std::size_t i = 1; i < 9; ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));

  const DrawText &question = std::get<DrawText>(r.log[9]);
  CHECK(question.text == "Enter?");
  CHECK(question.colour.r == style.body.r);

  const DrawText &yes_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &yes_label = std::get<DrawText>(r.log[11]);
  const DrawText &no_cursor = std::get<DrawText>(r.log[12]);
  const DrawText &no_label = std::get<DrawText>(r.log[13]);

  CHECK(yes_cursor.text == "> ");
  CHECK(yes_label.text == "Yes");
  CHECK(yes_cursor.colour.r == style.selected.r);
  CHECK(no_cursor.text == "  ");
  CHECK(no_label.text == "No");
  CHECK(no_cursor.colour.r == style.choice.r);
  CHECK(no_label.position.y == yes_label.position.y); // both options share the row
  CHECK(no_label.position.x > yes_label.position.x);
}

TEST_CASE("prompt_box_render: the option row is centered within the panel") {
  // Regression: the row's width must count the cursor column ahead of both labels, or the
  // row is placed half a cursor-width right of center.
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::prompt_box_render(r, style, border, "Enter?", true, viewport);

  const DrawRect &panel = std::get<DrawRect>(r.log[0]);
  const DrawText &yes_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &no_label = std::get<DrawText>(r.log[13]);

  const float no_w = r.measure_text(style.font_id, "No", style.font_size_body);
  const float row_left = yes_cursor.position.x;
  const float row_right = no_label.position.x + no_w;
  const float panel_center = panel.position.x + (panel.size.x * 0.5f);

  CHECK((row_left + row_right) * 0.5f == panel_center);
}

TEST_CASE("prompt_box_render: selection swaps the cursor without moving the label columns") {
  RecordingRenderer r_yes;
  RecordingRenderer r_no;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::prompt_box_render(r_yes, style, border, "Leave?", true, viewport);
  corundum::ui::prompt_box_render(r_no, style, border, "Leave?", false, viewport);

  // Cursors follow the highlighted option...
  CHECK(std::get<DrawText>(r_yes.log[10]).text == "> ");
  CHECK(std::get<DrawText>(r_yes.log[12]).text == "  ");
  CHECK(std::get<DrawText>(r_no.log[10]).text == "  ");
  CHECK(std::get<DrawText>(r_no.log[12]).text == "> ");

  // ...but the label columns stay put, since draw_option always advances by the cursor width.
  CHECK(std::get<DrawText>(r_yes.log[11]).position.x == std::get<DrawText>(r_no.log[11]).position.x);
  CHECK(std::get<DrawText>(r_yes.log[13]).position.x == std::get<DrawText>(r_no.log[13]).position.x);

  // Colours also follow selection.
  CHECK(std::get<DrawText>(r_yes.log[11]).colour.r == style.selected.r);
  CHECK(std::get<DrawText>(r_no.log[11]).colour.r == style.choice.r);
}

TEST_CASE("prompt_box_render: panel respects the minimum width and grows for a long question") {
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  {
    RecordingRenderer r;
    corundum::ui::prompt_box_render(r, style, border, "Enter?", true, viewport);
    CHECK(std::get<DrawRect>(r.log[0]).size.x == 200.f);
  }
  {
    RecordingRenderer r;
    const std::string_view long_question = "A considerably longer question?";
    corundum::ui::prompt_box_render(r, style, border, long_question, true, viewport);
    const float q_w = r.measure_text(style.font_id, long_question, style.font_size_body);
    CHECK(std::get<DrawRect>(r.log[0]).size.x == q_w + 64.f); // content + 2 × k_pad_x
  }
}

// ── dialog_box_update ─────────────────────────────────────────────────────────

namespace {

  // Builds a Talk graph with the requested graph_id, speaker, and a node literally
  // named "n0". Used to reproduce the Keystone bug where two NPCs share a first-node
  // id but have different speakers.
  corundum::dialogue::Graph make_talk_graph(std::string graph_id, std::string speaker, std::string talk_text) {
    using namespace corundum::dialogue;
    Graph g;
    g.graph_id = std::move(graph_id);
    g.speaker = std::move(speaker);
    Node n;
    n.id = "n0";
    n.type = NodeType::Talk;
    n.text = std::move(talk_text);
    n.next_id = "end";
    g.id_to_index[n.id] = 0;
    g.nodes.push_back(std::move(n));
    return g;
  }

  // Builds a Choice graph where the second option is gated by quest_is_at.
  // Used to verify that threading the quest registry through dialog_box_update
  // yields the gated choice in the layout (not hidden by a parse failure).
  corundum::dialogue::Graph make_choice_graph_with_quest_gate() {
    using namespace corundum::dialogue;
    Graph g;
    g.graph_id = "gated";
    g.speaker = "Gatekeeper";
    Node n;
    n.id = "n0";
    n.type = NodeType::Choice;
    n.choices = {
        {.label = "Always.", .target_id = "a"},
        {.label = "Secret.", .target_id = "b", .condition = *compile("quest_is_at(ember, done)")},
    };
    g.id_to_index[n.id] = 0;
    g.nodes.push_back(std::move(n));
    return g;
  }

  // Builds a Choice graph whose second option is gated by a plain boolean flag,
  // for exercising a visibility change at an otherwise unchanged node.
  corundum::dialogue::Graph make_flag_gated_choice_graph() {
    using namespace corundum::dialogue;
    Graph g;
    g.graph_id = "flag_gated";
    g.speaker = "Gatekeeper";
    Node n;
    n.id = "n0";
    n.type = NodeType::Choice;
    n.choices = {
        {.label = "Always.", .target_id = "a"},
        {.label = "Secret.", .target_id = "b", .condition = *compile("secret == true")},
    };
    g.id_to_index[n.id] = 0;
    g.nodes.push_back(std::move(n));
    return g;
  }

} // namespace

TEST_CASE("dialog_box_update: switching graphs with a shared first-node id rebuilds the layout") {
  // Reproduces the Keystone cancel + retalk bug: every dialogue file starts at a
  // node named "n0". After cancelling the innkeeper and starting the villager, the
  // stale-check must see that the graph id changed and rebuild — otherwise the
  // innkeeper's speaker/text stays on screen.
  RecordingRenderer r;
  corundum::ui::DialogBoxState ds{};
  ds.border = make_border();

  const auto innkeeper = make_talk_graph("innkeeper_intro", "Innkeeper", "Welcome, traveller.");
  const auto villager = make_talk_graph("villager_generic", "Villager", "Did you see the harvest moon last night?");

  corundum::world::FlagStore flags;
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  const corundum::dialogue::Conversation innkeeper_conversation{innkeeper, flags};
  corundum::ui::dialog_box_update(ds, innkeeper_conversation, r, viewport);
  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  CHECK(ds.layout->speaker == "Innkeeper");
  CHECK_FALSE(ds.layout->body_lines.empty());
  // NOLINTEND(bugprone-unchecked-optional-access)

  // Cancel and switch NPCs.
  const corundum::dialogue::Conversation villager_conversation{villager, flags};
  corundum::ui::dialog_box_update(ds, villager_conversation, r, viewport);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  CHECK(ds.layout->speaker == "Villager");
  REQUIRE_FALSE(ds.layout->body_lines.empty());
  CHECK(ds.layout->body_lines.front() == "Did you see the harvest moon last night?");
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("dialog_box_update: an ended conversation hides the box") {
  // Regression: the render system only calls dialog_box_update while scene.dialogue is
  // engaged, and update_dialogue() resets the optional the frame the conversation
  // ends. The box must be hidden by that frame — never repainted from a stale layout.
  RecordingRenderer r;
  corundum::ui::DialogBoxState ds{};
  ds.border = make_border();

  const auto graph = make_talk_graph("innkeeper_intro", "Innkeeper", "Welcome, traveller.");
  corundum::world::FlagStore flags;
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::dialogue::Conversation conversation{graph, flags};
  corundum::ui::dialog_box_update(ds, conversation, r, viewport);
  REQUIRE(ds.visible);
  REQUIRE(ds.layout.has_value());

  // The Talk node's next is "end", so Select closes the conversation.
  corundum::input::PressedActions select{};
  select.actions[0] = corundum::input::Action::Select;
  select.count = 1;
  static_cast<void>(conversation.update(select));
  CHECK_FALSE(conversation.is_active());

  // The frame after the conversation ends, the box must be hidden, not stale-visible.
  corundum::ui::dialog_box_update(ds, conversation, r, viewport);
  CHECK_FALSE(ds.visible);
}

TEST_CASE("dialog_box_update: quest-gated choice is drawn when the registry is threaded") {
  // Verifies the render-side end of Bug 1: a Choice node with a quest_is_at gate
  // is rendered with the gated option present when dialog_box_update receives the
  // registry and the matching quest.<id> flag is set. Before the fix, the parse
  // would error on a null registry and the choice would be hidden.
  RecordingRenderer r;
  corundum::ui::DialogBoxState ds{};
  ds.border = make_border();

  corundum::quest::Registry quests;
  corundum::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  quests.add(std::move(q));

  const auto graph = make_choice_graph_with_quest_gate();

  corundum::world::FlagStore flags;
  flags["quest.ember"] = 2; // matches stage "done" (sequence 2)
  const corundum::dialogue::Conversation conversation{graph, flags, &quests};

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::dialog_box_update(ds, conversation, r, viewport);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  REQUIRE(ds.layout->choices.size() == 2);
  REQUIRE(ds.layout->choices[0].lines.size() == 1);
  REQUIRE(ds.layout->choices[1].lines.size() == 1);
  CHECK(ds.layout->choices[0].lines.front() == "Always.");
  CHECK(ds.layout->choices[1].lines.front() == "Secret.");
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("dialog_box_update: a vertical-only viewport change rebuilds the panel geometry") {
  // Regression: the stale check tracked only panel width, so growing the window
  // vertically left the box pinned at its old height and offset (build_layout derives
  // panel_h and panel_y from viewport.y).
  RecordingRenderer r;
  corundum::ui::DialogBoxState ds{};
  ds.border = make_border();

  const auto graph = make_talk_graph("innkeeper_intro", "Innkeeper", "Welcome, traveller.");
  corundum::world::FlagStore flags;
  const corundum::dialogue::Conversation conversation{graph, flags};

  corundum::ui::dialog_box_update(ds, conversation, r, {.x = 1280.f, .y = 720.f});
  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  const float first_h = ds.layout->panel_size.y;
  const float first_y = ds.layout->panel_pos.y;
  // NOLINTEND(bugprone-unchecked-optional-access)

  // Same width, taller viewport.
  corundum::ui::dialog_box_update(ds, conversation, r, {.x = 1280.f, .y = 900.f});

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  CHECK(ds.layout->panel_size.y > first_h);
  CHECK(ds.layout->panel_pos.y != first_y);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("dialog_box_update: a visibility change at the same node rebuilds the layout") {
  // Regression: the cache key was (graph, node, viewport). A Choice node re-visited with
  // different flags (quest progress between visits, or a goto_graph loop) kept the old
  // condition-evaluated choice list.
  RecordingRenderer r;
  corundum::ui::DialogBoxState ds{};
  ds.border = make_border();

  const auto graph = make_flag_gated_choice_graph();
  corundum::world::FlagStore flags;
  const corundum::dialogue::Conversation conversation{graph, flags};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::dialog_box_update(ds, conversation, r, viewport);
  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  REQUIRE(ds.layout->choices.size() == 1);
  CHECK(ds.layout->choices[0].lines.front() == "Always.");
  // NOLINTEND(bugprone-unchecked-optional-access)

  // Quest progress unlocks the gated option while the node stays the same.
  flags["secret"] = 1;
  corundum::ui::dialog_box_update(ds, conversation, r, viewport);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  REQUIRE(ds.layout->choices.size() == 2);
  CHECK(ds.layout->choices[1].lines.front() == "Secret.");
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("build_layout: a choice label wider than the panel keeps every wrapped line") {
  // Regression: the layout kept only the first wrapped line of a choice label, silently
  // truncating any option too long for the panel.
  RecordingRenderer r;
  using namespace corundum::dialogue;

  Graph graph;
  graph.graph_id = "wrapped";
  Node node;
  node.id = "n0";
  node.type = NodeType::Choice;
  node.choices = {{.label = "Alpha Beta Gamma Delta", .target_id = "a"}};
  graph.id_to_index[node.id] = 0;
  graph.nodes.push_back(std::move(node));

  corundum::world::FlagStore flags;
  const corundum::dialogue::Conversation conversation{graph, flags};

  // Narrow viewport: choice_w = 200 - 2*20 (margin) - 2*20 (inset) - 16 (cursor) = 104px,
  // i.e. 13 glyphs at the recorder's 8px each.
  const corundum::ui::DialogLayout layout =
      corundum::ui::build_layout(conversation, 20.f, 0.32f, 4, {.x = 200.f, .y = 720.f},
                                 [&](std::string_view text) { return r.measure_text(0, text, 22); });

  REQUIRE(layout.choices.size() == 1);
  REQUIRE(layout.choices[0].lines.size() > 1);

  // Re-joining the wrapped lines recovers every word of the label.
  std::string joined;
  for (const std::string &line : layout.choices[0].lines) {
    if (!joined.empty())
      joined += ' ';
    joined += line;
  }
  CHECK(joined == "Alpha Beta Gamma Delta");
}

TEST_CASE("dialog_box_render: wrapped continuation lines keep the selected colour and hanging indent") {
  // A selected choice whose label wrapped: only the first line shows the "> " cursor, but
  // every line stays highlighted (style.selected) and aligned in the label column.
  RecordingRenderer r;
  corundum::ui::DialogBoxState ds{};
  ds.border = make_border();
  ds.visible = true;

  corundum::ui::DialogLayout layout{};
  layout.panel_pos = {.x = 0.f, .y = 0.f};
  layout.panel_size = {.x = 400.f, .y = 200.f};
  layout.inset = 10.f;
  layout.node_type = corundum::dialogue::NodeType::Choice;
  layout.choices = {
      corundum::ui::ChoiceLayout{.index = 0, .lines = {"first line", "second line"}},
      corundum::ui::ChoiceLayout{.index = 1, .lines = {"other"}},
  };
  ds.layout = std::move(layout);

  corundum::ui::dialog_box_render(ds, r);

  // chrome (1 rect + 8 sprites) + "Choose:" header + choice 0 (2 lines × 2) + choice 1 (1 × 2).
  REQUIRE(r.log.size() == 9 + 1 + 4 + 2);
  const float advance = corundum::ui::cursor_advance(r, ds.style);

  // First line: "> " cursor in the selected colour, label in the cursor column.
  CHECK(std::get<DrawText>(r.log[10]).text == "> ");
  CHECK(std::get<DrawText>(r.log[11]).colour.r == ds.style.selected.r);
  CHECK(std::get<DrawText>(r.log[11]).position.x == 10.f + advance); // px + inset + advance

  // Second line: no cursor, same colour, same hanging indent.
  CHECK(std::get<DrawText>(r.log[12]).text == "  ");
  CHECK(std::get<DrawText>(r.log[13]).colour.r == ds.style.selected.r);
  CHECK(std::get<DrawText>(r.log[13]).position.x == std::get<DrawText>(r.log[11]).position.x);
}

// ── inventory_panel_render ───────────────────────────────────────────────────

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("inventory_panel_render: 2 rows emit chrome, header, and one option pair per row") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};

  const std::vector<corundum::ui::InventoryLine> lines = {
      {.count = 2, .name = "Apple"},
      {.count = 1, .name = "Salt"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::inventory_panel_render(r, style, border, lines, 0, viewport);

  // panel_chrome: 1 DrawRect + 8 DrawSprite; then the "Inventory" header DrawText;
  // then 2 rows × 2 DrawText (cursor + label).
  REQUIRE(r.log.size() == 9 + 1 + 4);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
  for (std::size_t i = 1; i < 9; ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));

  const DrawText &header = std::get<DrawText>(r.log[9]);
  CHECK(header.text == "Inventory");
  CHECK(header.colour.r == style.speaker.r);

  const DrawText &row0_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &row0_label = std::get<DrawText>(r.log[11]);
  const DrawText &row1_cursor = std::get<DrawText>(r.log[12]);
  const DrawText &row1_label = std::get<DrawText>(r.log[13]);

  CHECK(row0_cursor.text == "> ");
  CHECK(row0_label.text == "Apple  x2");
  CHECK(row0_cursor.colour.r == style.selected.r); // cursor row uses style.selected
  CHECK(row0_label.colour.r == style.selected.r);

  CHECK(row1_cursor.text == "  ");
  CHECK(row1_label.text == "Salt  x1");
  CHECK(row1_cursor.colour.r == style.choice.r); // non-cursor row uses style.choice
  CHECK(row1_label.colour.r == style.choice.r);
  CHECK(row1_label.position.y > row0_label.position.y); // rows stack downward
}

TEST_CASE("inventory_panel_render: empty list renders header plus one (empty) line") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::inventory_panel_render(r, style, border, {}, 0, viewport);

  // Chrome (9) + header + one "(empty)" line.
  REQUIRE(r.log.size() == 9 + 1 + 1);
  const DrawText &header = std::get<DrawText>(r.log[9]);
  CHECK(header.text == "Inventory");
  const DrawText &empty = std::get<DrawText>(r.log[10]);
  CHECK(empty.text == "(empty)");
}

TEST_CASE("inventory_panel_render: cursor is clamped into the row range") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};

  const std::vector<corundum::ui::InventoryLine> lines = {
      {.count = 1, .name = "A"},
      {.count = 1, .name = "B"},
      {.count = 1, .name = "C"},
  };
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  // cursor 99 → clamps to the last row.
  corundum::ui::inventory_panel_render(r, style, border, lines, 99, viewport);
  const DrawText &last_cursor = std::get<DrawText>(r.log[r.log.size() - 2]);
  CHECK(last_cursor.text == "> ");
  const DrawText &last_label = std::get<DrawText>(r.log.back());
  CHECK(last_label.text == "C  x1");

  // cursor -5 → clamps to the first row.
  RecordingRenderer r2;
  corundum::ui::inventory_panel_render(r2, style, border, lines, -5, viewport);
  const DrawText &first_cursor = std::get<DrawText>(r2.log[10]);
  CHECK(first_cursor.text == "> ");
  const DrawText &first_label = std::get<DrawText>(r2.log[11]);
  CHECK(first_label.text == "A  x1");
}

// ── build_inventory_lines ────────────────────────────────────────────────────

TEST_CASE("build_inventory_lines: skips zero counts and non-item flags, sorts by name, falls back to id") {
  corundum::world::FlagStore flags;
  flags["item.a"] = 2;
  flags["item.b"] = 0;  // zero count → dropped
  flags["item.c"] = 1;  // unknown to the registry → id fallback
  flags["quest.x"] = 3; // non-item key → ignored

  corundum::item::Registry items;
  corundum::item::Item a;
  a.id = "a";
  a.name = "Apple";
  items.add(std::move(a));

  const auto lines = corundum::ui::build_inventory_lines(flags, items);

  REQUIRE(lines.size() == 2);
  CHECK(lines[0].name == "Apple");
  CHECK(lines[0].count == 2);
  CHECK(lines[1].name == "c");
  CHECK(lines[1].count == 1);
}

TEST_CASE("build_inventory_lines: groups items by category, ordering by (category, name)") {
  using corundum::item::ItemCategory;

  corundum::world::FlagStore flags;
  flags["item.zzz"] = 1; // Misc — category order puts it second
  flags["item.aaa"] = 1; // Weapon
  flags["item.mmm"] = 1; // Apparel
  flags["item.bbb"] = 1; // Potion

  corundum::item::Registry items;
  corundum::item::Item weapon;
  weapon.id = "aaa";
  weapon.name = "Axe";
  weapon.category = ItemCategory::Weapon;
  items.add(std::move(weapon));

  corundum::item::Item apparel;
  apparel.id = "mmm";
  apparel.name = "Cloak";
  apparel.category = ItemCategory::Apparel;
  items.add(std::move(apparel));

  corundum::item::Item potion;
  potion.id = "bbb";
  potion.name = "Draught";
  potion.category = ItemCategory::Potion;
  items.add(std::move(potion));

  const auto lines = corundum::ui::build_inventory_lines(flags, items);

  REQUIRE(lines.size() == 4);
  // Enum order is Apparel < Misc < Potion < Weapon; names sort within a category.
  CHECK(lines[0].name == "Cloak");
  CHECK(lines[0].category == ItemCategory::Apparel);
  CHECK(lines[1].name == "zzz");
  CHECK(lines[1].category == ItemCategory::Misc);
  CHECK(lines[2].name == "Draught");
  CHECK(lines[2].category == ItemCategory::Potion);
  CHECK(lines[3].name == "Axe");
  CHECK(lines[3].category == ItemCategory::Weapon);
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("inventory_panel_render: category groups draw one header per group, in category order") {
  using corundum::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};

  // Pre-sorted as build_inventory_lines would produce: (category, name).
  const std::vector<corundum::ui::InventoryLine> lines = {
      {.category = ItemCategory::Apparel, .count = 1, .name = "Cloak"},
      {.category = ItemCategory::Misc, .count = 1, .name = "zzz"},
      {.category = ItemCategory::Potion, .count = 1, .name = "Draught"},
      {.category = ItemCategory::Weapon, .count = 1, .name = "Axe"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::inventory_panel_render(r, style, border, lines, 0, viewport);

  // Chrome (9) + "Inventory" header + 4 group headers + 4 rows × 2 DrawText (cursor + label).
  REQUIRE(r.log.size() == 9 + 1 + 4 + 8);

  std::vector<std::string> texts;
  for (const auto &call : r.log)
    if (std::holds_alternative<DrawText>(call))
      texts.emplace_back(std::get<DrawText>(call).text);

  CHECK(texts[0] == "Inventory");
  // Each group header immediately precedes its rows (cursor then label), in category
  // order; only the first row (cursor 0) is selected.
  CHECK(texts[1] == "Apparel");
  CHECK(texts[2] == "> ");
  CHECK(texts[3] == "Cloak  x1");
  CHECK(texts[4] == "Misc");
  CHECK(texts[5] == "  ");
  CHECK(texts[6] == "zzz  x1");
  CHECK(texts[7] == "Potion");
  CHECK(texts[8] == "  ");
  CHECK(texts[9] == "Draught  x1");
  CHECK(texts[10] == "Weapon");
  CHECK(texts[11] == "  ");
  CHECK(texts[12] == "Axe  x1");
}

TEST_CASE("inventory_panel_render: Misc-only inventory draws no group header") {
  using corundum::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};

  const std::vector<corundum::ui::InventoryLine> lines = {
      {.category = ItemCategory::Misc, .count = 1, .name = "Clutter"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::inventory_panel_render(r, style, border, lines, 0, viewport);

  // Chrome (9) + "Inventory" header + 1 row × 2. No "Misc" group header.
  REQUIRE(r.log.size() == 9 + 1 + 2);

  const DrawText &header = std::get<DrawText>(r.log[9]);
  CHECK(header.text == "Inventory");
  const DrawText &row_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &row_label = std::get<DrawText>(r.log[11]);
  CHECK(row_cursor.text == "> ");
  CHECK(row_label.text == "Clutter  x1");
}

TEST_CASE("inventory_panel_render: panel width reserves the cursor column and header width") {
  using corundum::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::DialogBoxStyle style{};

  // A name long enough to clear k_min_w: label 32 chars (256px) + "> " (16px) = 272 content.
  const std::vector<corundum::ui::InventoryLine> lines = {
      {.category = ItemCategory::Apparel, .count = 1, .name = "abcdefghijklmnopqrstuvwxyzab"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::inventory_panel_render(r, style, border, lines, 0, viewport);

  // measure_text is 8px/char: content = cursor (2) + label (32) = 34 chars, plus 24px pad each side.
  const DrawRect &panel = std::get<DrawRect>(r.log[0]);
  CHECK(panel.size.x == (34.f * 8.f) + (24.f * 2.f));
}

TEST_CASE("inventory_panel_render: speaker-sized group header claims its own row height") {
  using corundum::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  corundum::ui::DialogBoxStyle style{};
  style.font_size_body = 10;
  style.line_spacing = 12.f;
  style.font_size_speaker = 40;

  const std::vector<corundum::ui::InventoryLine> lines = {
      {.category = ItemCategory::Apparel, .count = 1, .name = "Cloak"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::ui::inventory_panel_render(r, style, border, lines, 0, viewport);

  // Chrome(9) + title + group header + cursor + label. Header row height is
  // max(body_line_h, speaker + 4) = max(14, 44) = 44, not the body row height.
  const DrawText &group_header = std::get<DrawText>(r.log[10]);
  const DrawText &row_cursor = std::get<DrawText>(r.log[11]);
  CHECK(group_header.text == "Apparel");
  CHECK(row_cursor.position.y - group_header.position.y == 44.f);

  const DrawRect &panel = std::get<DrawRect>(r.log[0]);
  CHECK(panel.size.y == (16.f * 2.f) + 44.f + 10.f + 14.f + 44.f);
}
