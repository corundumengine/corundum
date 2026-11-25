// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/save_load.hpp>

#include "gameplay/screens/screen_handlers.hpp"
#include "gameplay/screens/screen_input.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/save/save.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

namespace {
constexpr float k_save_load_pad_x = 28.f;
constexpr float k_save_load_pad_y = 18.f;
constexpr float k_save_load_title_gap = 12.f;
constexpr float k_save_load_footer_gap = 14.f;
constexpr float k_save_load_column_gap = 24.f;
constexpr float k_save_load_list_fraction = 0.45f;

std::string slot_list_label(const save::SaveSlotInfo &info) {
  std::string name = slot_display_name(info.slot_id);
  if (!info.error.empty())
    return std::format("{} (corrupt)", name);
  if (!info.meta.has_value())
    return std::format("{} (empty)", name);
  return name;
}

std::string playtime_text(std::int64_t seconds) {
  const std::int64_t clamped = std::max<std::int64_t>(seconds, 0);
  const std::int64_t hours = clamped / 3600;
  const std::int64_t minutes = (clamped % 3600) / 60;
  const std::int64_t secs = clamped % 60;
  return std::format("Playtime: {:02}:{:02}:{:02}", hours, minutes, secs);
}

std::string saved_at_text(std::int64_t unix_seconds) {
  const auto time = static_cast<std::time_t>(unix_seconds);
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &time) != 0)
    return "Saved: unknown";
#else
  if (localtime_r(&time, &local) == nullptr)
    return "Saved: unknown";
#endif
  std::array<char, 32> buffer{};
  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M", &local) == 0)
    return "Saved: unknown";
  return std::format("Saved: {}", buffer.data());
}

std::vector<std::string> detail_lines(const save::SaveSlotInfo &info) {
  if (!info.error.empty())
    return {std::string{"Corrupt save"}, info.error};
  if (!info.meta.has_value())
    return {"(empty)"};
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the branch above guarantees a value.
  const save::SaveMeta &meta = info.meta.value();
  std::vector<std::string> lines;
  lines.push_back(meta.location_name.empty() ? std::string{"Unknown location"} : meta.location_name);
  lines.push_back(playtime_text(meta.playtime_seconds));
  lines.push_back(saved_at_text(meta.saved_at_unix));
  return lines;
}
} // namespace

std::string slot_display_name(std::string_view slot_id) {
  if (slot_id == save::k_autosave_slot)
    return "Autosave";
  if (slot_id == save::k_quicksave_slot)
    return "Quicksave";
  constexpr std::string_view k_prefix{"slot_"};
  if (slot_id.starts_with(k_prefix))
    return std::format("Slot {}", slot_id.substr(k_prefix.size()));
  return std::string{slot_id};
}

bool slot_is_empty(const save::SaveSlotInfo &info) noexcept {
  return !info.meta.has_value() && info.error.empty();
}

std::vector<save::SaveSlotInfo> build_slot_rows(const core::UserStorage &storage, std::string_view game_id) {
  const std::vector<save::SaveSlotInfo> existing = save::list_saves(storage, game_id);
  std::map<std::string, save::SaveSlotInfo, std::less<>> by_id;
  for (const save::SaveSlotInfo &info : existing)
    by_id.emplace(info.slot_id, info);

  std::vector<save::SaveSlotInfo> rows;
  rows.reserve(static_cast<std::size_t>(save::k_manual_slot_count) + 2);

  // Autosave and quicksave lead only when they exist: an unwritten quicksave is not a slot the
  // player can browse to.
  for (const std::string_view named : {save::k_autosave_slot, save::k_quicksave_slot}) {
    if (const auto it = by_id.find(named); it != by_id.end())
      rows.push_back(it->second);
  }
  for (int index = 0; index < save::k_manual_slot_count; ++index) {
    const std::string id = save::manual_slot_id(index);
    if (const auto it = by_id.find(id); it != by_id.end()) {
      rows.push_back(it->second);
    } else {
      rows.push_back(save::SaveSlotInfo{.slot_id = id});
    }
  }
  return rows;
}

std::optional<std::string> newest_valid_slot(const std::vector<save::SaveSlotInfo> &slots) {
  std::optional<std::string> newest;
  std::int64_t newest_time = 0;
  for (const save::SaveSlotInfo &info : slots) {
    if (!info.meta.has_value())
      continue;
    if (info.meta->saved_at_unix < newest_time)
      continue;
    newest_time = info.meta->saved_at_unix;
    newest = info.slot_id;
  }
  return newest;
}

SaveLoadLayout save_load_panel_layout(const platform::Renderer & /*r*/, const ui::PanelStyle &style,
                                      const SaveLoadState &state, core::math::Vec2 viewport,
                                      input::InputDevice /*last_device*/) {
  const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
  const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
  const ui::PanelRect panel = ui::screen_panel_rect(viewport, style, 0.f);

  const float content_x = panel.pos.x + k_save_load_pad_x;
  const float content_w = panel.size.x - (k_save_load_pad_x * 2.f);
  const float list_w = content_w * k_save_load_list_fraction;
  const float detail_x = content_x + list_w + k_save_load_column_gap;
  const float detail_w = std::max(0.f, content_w - list_w - k_save_load_column_gap);

  const float list_top = panel.pos.y + k_save_load_pad_y + title_h + k_save_load_title_gap;
  const float footer_y = panel.pos.y + panel.size.y - k_save_load_pad_y - line_h;
  const float list_bottom = footer_y - k_save_load_footer_gap;

  const int row_count = static_cast<int>(state.slots.size());
  const int fits = std::max(1, static_cast<int>((list_bottom - list_top) / line_h));
  const int visible_rows = std::min(row_count, fits);
  const int first_row = ui::clamp_scroll_to_cursor(state.scroll, state.cursor, row_count, visible_rows);

  SaveLoadLayout layout{};
  layout.panel_pos = panel.pos;
  layout.panel_size = panel.size;
  layout.rows = ui::ListHit{
      .row_pos = {.x = content_x, .y = list_top},
      .row_width = list_w,
      .row_height = line_h,
      .first_row = first_row,
      .visible_rows = visible_rows,
  };
  layout.detail = ui::RowRect{
      .pos = {.x = detail_x, .y = list_top},
      .width = detail_w,
      .height = std::max(0.f, list_bottom - list_top),
  };
  return layout;
}

void save_load_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style,
                            const ui::NinePatchBorder &border, const SaveLoadState &state, core::math::Vec2 viewport,
                            input::InputDevice last_device) {
  const SaveLoadLayout layout = save_load_panel_layout(renderer, style, state, viewport, last_device);
  const float line_h = layout.rows.row_height;
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  const std::string title = state.saving ? "Save Game" : "Load Game";
  const std::string footer = std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                                         ui::input_glyph(input::Action::Cancel, last_device));

  ui::screen_backdrop(renderer, style, viewport);
  ui::panel_chrome(renderer, style.bg, border, layout.panel_pos, layout.panel_size);

  const float title_w = renderer.measure_text(font_id, title, style.font_size_speaker);
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = title,
      .position =
          {
              .x = layout.panel_pos.x + ((layout.panel_size.x - title_w) * 0.5f),
              .y = layout.panel_pos.y + k_save_load_pad_y,
          },
      .char_size = style.font_size_speaker,
      .colour = style.speaker,
  });

  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = footer,
      .position =
          {
              .x = layout.panel_pos.x +
                   ((layout.panel_size.x - renderer.measure_text(font_id, footer, style.font_size_body)) * 0.5f),
              .y = layout.panel_pos.y + layout.panel_size.y - k_save_load_pad_y - line_h,
          },
      .char_size = style.font_size_body,
      .colour = style.choice,
  });

  if (state.slots.empty()) {
    renderer.draw(platform::DrawText{
        .font_id = font_id,
        .text = "(no saves)",
        .position = layout.rows.row_pos,
        .char_size = style.font_size_body,
        .colour = style.choice,
    });
    return;
  }

  const int clamped_cursor = std::clamp(state.cursor, 0, static_cast<int>(state.slots.size()) - 1);
  float y = layout.rows.row_pos.y;
  const int last_row = std::min(layout.rows.first_row + layout.rows.visible_rows, static_cast<int>(state.slots.size()));
  for (int i = layout.rows.first_row; i < last_row; ++i) {
    ui::draw_option(renderer, style, slot_list_label(state.slots[static_cast<std::size_t>(i)]),
                    {.x = layout.rows.row_pos.x, .y = y}, i == clamped_cursor);
    y += line_h;
  }

  const save::SaveSlotInfo &selected = state.slots[static_cast<std::size_t>(clamped_cursor)];
  const std::vector<std::string> lines = detail_lines(selected);
  float detail_y = layout.detail.pos.y;
  for (const std::string &line : lines) {
    renderer.draw(platform::DrawText{
        .font_id = font_id,
        .text = line,
        .position = {.x = layout.detail.pos.x, .y = detail_y},
        .char_size = style.font_size_body,
        .colour = style.body,
    });
    detail_y += line_h;
  }
}

namespace {

/** Save `slot_id`, raising a success or failure toast, then pop the Save/Load screen. */
void commit_slot_save(Engine &engine, Gameplay &gameplay, const std::string &slot_id) {
  if (const std::expected<void, std::string> result = gameplay.save_to_slot(slot_id); !result)
    engine.notify(std::format("Save failed: {}", result.error()), ui::k_toast_failed_colour);
  else
    engine.notify("Game saved");
  engine.scene.ui.pop();
}

/** Save the selected row, first confirming when it would overwrite an existing slot. The
 *  autosave slot is never overwritable from the UI; the game decides when to autosave.
 */
void activate_save_load_row(Engine &engine, Gameplay &gameplay, const save::SaveSlotInfo &selected) {
  const std::string slot_id = selected.slot_id;
  if (slot_id == save::k_autosave_slot) {
    engine.notify("Autosave cannot be overwritten");
    return;
  }

  const bool occupied = selected.meta.has_value() || !selected.error.empty();
  if (!occupied) {
    commit_slot_save(engine, gameplay, slot_id);
    return;
  }
  gameplay.open_confirm(std::format("Overwrite {}?", slot_display_name(slot_id)),
                        // The confirm callback runs inside the noexcept fixed-step loop, where a throw is fatal
                        // by engine policy (see run_fixed_steps); the escape is intentional here.
                        // NOLINTNEXTLINE(bugprone-exception-escape)
                        [&engine, slot_id](Gameplay &g) { commit_slot_save(engine, g, slot_id); });
}

/** Apply mouse intent to the save/load screen: a hovered row takes focus and the wheel moves a row. */
void apply_save_load_pointer(const Engine &engine, SaveLoadState &state, const input::InputIntent &intent, int rows) {
  if (!pointer_active(intent))
    return;
  const SaveLoadLayout layout = save_load_panel_layout(engine.renderer(), engine.render.panel_skin.style, state,
                                                       screen_viewport(engine), engine.input_mapper.last_device());
  const core::math::Vec2 cursor = intent_cursor(intent);
  if (pointer_focus(intent)) {
    if (const int hovered = ui::hovered_row(cursor, layout.rows); hovered >= 0)
      state.cursor = layout.rows.first_row + hovered;
  }
  if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0 && rows > 0)
    state.cursor = wrap_cursor(state.cursor, steps, rows);
}

/** Save or load the selected slot, honouring the screen's mode. */
void activate_selected_slot(Engine &engine, Gameplay &gameplay, const SaveLoadState &state,
                            const save::SaveSlotInfo &selected) {
  if (state.saving) {
    activate_save_load_row(engine, gameplay, selected);
    return;
  }
  if (slot_is_empty(selected) || !selected.error.empty())
    return;
  const std::string slot_id = selected.slot_id;
  // The queued callback runs inside the noexcept fixed-step loop, where a throw is fatal by
  // engine policy (see run_fixed_steps); the escape is intentional here.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  gameplay.begin_load([slot_id](Gameplay &g) { return g.load_from_slot(slot_id); });
}

} // namespace

void update_save_load(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  SaveLoadState &state = gameplay.save_load_screen;
  if (intent.back) {
    engine.scene.ui.pop();
    return;
  }

  const int rows = static_cast<int>(state.slots.size());
  apply_save_load_pointer(engine, state, intent, rows);

  if (intent.navigate_y != 0) {
    if (rows <= 0)
      state.cursor = 0;
    else
      state.cursor = wrap_cursor(state.cursor, intent.navigate_y, rows);
  }

  const SaveLoadLayout visible = save_load_panel_layout(engine.renderer(), engine.render.panel_skin.style, state,
                                                        screen_viewport(engine), engine.input_mapper.last_device());
  state.scroll = visible.rows.first_row;

  if (!intent.activate || rows == 0)
    return;

  const save::SaveSlotInfo &selected = state.slots[static_cast<std::size_t>(std::clamp(state.cursor, 0, rows - 1))];
  activate_selected_slot(engine, gameplay, state, selected);
}

void render_save_load(const Engine &engine, const SaveLoadState &state, platform::Renderer &renderer,
                      core::math::Vec2 viewport) {
  if (engine.scene.mode() != SaveLoad)
    return;
  save_load_panel_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, state, viewport,
                         engine.input_mapper.last_device());
}

} // namespace corundum::gameplay::screens
