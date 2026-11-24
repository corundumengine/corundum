// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/styled_text.hpp>

#include <span>
#include <string_view>

namespace corundum::ui {

/** Screen-space hit rectangle of one selectable row. */
struct RowRect {
  core::math::Vec2 pos{};

  float width{};

  float height{};
};

/** A screen-space panel rectangle: top-left plus extent. */
struct PanelRect {
  core::math::Vec2 pos{};

  core::math::Vec2 size{};
};

/** Maximum width, in logical pixels, of a viewport-filling screen panel. */
inline constexpr float k_screen_panel_max_width = 1600.f;

/** Geometry of a screen panel that fills the viewport.
 *
 *  The panel spans the viewport minus PanelStyle::margin on every side, with `top_inset`
 *  additionally reserved above it so a hub tab strip can sit in that gap. Its width is capped
 *  at k_screen_panel_max_width and centered when the viewport is wider, so an ultrawide display
 *  does not stretch rows across the whole screen. Height is not capped.
 *
 *  @param viewport  Screen size in logical pixels.
 *  @param style     Supplies PanelStyle::margin.
 *  @param top_inset Extra space reserved above the panel (e.g. hub strip y + line height).
 */
[[nodiscard]] PanelRect screen_panel_rect(core::math::Vec2 viewport, const PanelStyle &style, float top_inset = 0.f);

/** Screen-space hit geometry of a uniform vertical option list.
 *
 *  Shared by a screen's render and its mouse handling so the two cannot disagree about where
 *  a row is. Coordinates are logical window points (ui_scale is baked into the panel style),
 *  exactly the space InputIntent::cursor_x/cursor_y use.
 */
struct ListHit {
  /** Top-left of the first visible row's hit rectangle. */
  core::math::Vec2 row_pos{};

  /** Hit width of every row. */
  float row_width{};

  /** Row stride. */
  float row_height{};

  /** Index of the topmost visible row (a scroll offset). */
  int first_row{};

  /** Number of rows actually drawn. */
  int visible_rows{};
};

/** Index of the row under `cursor` in a uniform list, or -1.
 *
 *  A cursor on a row's edges counts as inside. `list.visible_rows` entries starting at
 *  `list.first_row` are hit; the returned index is absolute (already includes the offset).
 */
[[nodiscard]] int hovered_row(core::math::Vec2 cursor, const ListHit &list) noexcept;

/** Index of the RowRect under `cursor`, or -1.
 *
 *  For lists whose rows have non-uniform y (group headers interleaved with options), which
 *  a uniform ListHit cannot describe.
 */
[[nodiscard]] int hovered_row(core::math::Vec2 cursor, std::span<const RowRect> rows) noexcept;

/** Whole rows a scroll delta should move a list cursor.
 *
 *  Positive wheel (away from the user) moves focus toward the top of the list, matching how
 *  the codex body scrolls. Fractional deltas truncate toward zero.
 */
[[nodiscard]] int scroll_row_delta(float scroll_y) noexcept;

/** Scroll offset that keeps `cursor` inside a window of `visible_rows` rows.
 *
 *  Clamps `scroll` into `[0, row_count - visible_rows]`, then nudges it so `cursor` lies in
 *  `[scroll, scroll + visible_rows)`. Returns 0 when the whole list fits.
 *
 *  @param scroll       Requested first visible row.
 *  @param cursor       Row that must stay visible.
 *  @param row_count    Total number of rows.
 *  @param visible_rows Number of rows the window can show.
 */
[[nodiscard]] int clamp_scroll_to_cursor(int scroll, int cursor, int row_count, int visible_rows) noexcept;

/** Horizontal advance of the choice cursor column — the x-offset at which an
 *  option's label begins.
 *
 *  Measured against the role's regular face and the selected cursor (k_choice_cursor)
 *  regardless of selection, so a selected and an unselected option reserve the same column.
 *  Callers that lay out several columns (prompt/inventory) use this instead of re-measuring
 *  the glyph, keeping the one definition of the advance beside draw_option().
 *
 *  @param r     Renderer used for font metrics.
 *  @param style Supplies the role's font family and font_size_body.
 *  @param role  Font role the option is drawn in; defaults to the UI role.
 *  @return Width in pixels reserved for the cursor prefix.
 */
[[nodiscard]] float cursor_advance(const platform::Renderer &r, const PanelStyle &style, FontRole role = FontRole::Ui);

/** Fill `pos`/`size` with `bg` — the tint behind a panel's frame.
 *
 *  A translucent `bg` lets the scene show through and tint the panel; an opaque one makes a
 *  solid box. Kept separate from panel_frame() so a caller can draw either alone.
 *  @param r    Renderer; emits one DrawRect.
 *  @param bg   Fill colour.
 *  @param pos  Top-left in screen pixels.
 *  @param size Width/height in screen pixels.
 */
void panel_fill(platform::Renderer &r, core::math::Colour bg, core::math::Vec2 pos, core::math::Vec2 size);

/** Draw the `border` nine-patch frame around `pos`/`size`, with no fill.
 *
 *  A texture_id of TextureId::Invalid or a non-positive cell size skips the frame.
 *  @param r      Renderer; emits the border's DrawSprite commands.
 *  @param border Nine-patch frame texture/tile dims.
 *  @param pos    Top-left in screen pixels.
 *  @param size   Width/height in screen pixels.
 */
void panel_frame(platform::Renderer &r, const NinePatchBorder &border, core::math::Vec2 pos, core::math::Vec2 size);

/** panel_fill() then panel_frame() — the shared chrome of every nine-patch modal panel.
 *  @param r       Renderer; emits one DrawRect then the border's DrawSprite commands.
 *  @param bg      Panel fill colour (e.g. PanelStyle::bg).
 *  @param border  Nine-patch frame.
 *  @param pos     Top-left of the panel in screen pixels.
 *  @param size    Panel width/height in screen pixels.
 */
void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                  core::math::Vec2 size);

/** Fill the whole viewport with an opaque PanelStyle::bg.
 *
 *  Drawn behind a viewport-filling screen panel so the world does not show through the
 *  translucent PanelStyle::bg used for content-sized modals. One DrawRect per call.
 *
 *  @param r        Renderer; emits one DrawRect.
 *  @param style    Supplies bg; only its alpha is forced opaque.
 *  @param viewport Screen size in logical pixels.
 */
void screen_backdrop(platform::Renderer &r, const PanelStyle &style, core::math::Vec2 viewport);

/** Draw one selectable menu option: a "> " cursor (or two spaces when
 *         `show_cursor` is false) followed by `label`, coloured by `selected`.
 *
 *  The label always begins at `pos.x + cursor_advance(r, style)`.
 *
 *  @param r           Renderer; receives two DrawText commands (cursor, then label).
 *  @param style       Supplies the UI font family, font_size_body, and the selected/choice colours.
 *  @param label       Option text; drawn verbatim (no wrapping).
 *  @param pos         Top-left where the cursor starts.
 *  @param selected    True → style.selected; false → style.choice.
 *  @param show_cursor True → "> " when selected, "  " otherwise; false → always "  ".
 *                     Pass false for continuation lines of a wrapped option so they keep
 *                     the selected colour without repeating the cursor.
 */
void draw_option(platform::Renderer &r, const PanelStyle &style, std::string_view label, core::math::Vec2 pos,
                 bool selected, bool show_cursor = true);

/** draw_option() in an explicit `role` and `label_style`, for role-owned lists.
 *
 *  The cursor is always drawn in `role`'s regular face; only `label` uses `label_style`.
 *  Used by the quest-role screens (journal, codex) so their list rows follow the Quest
 *  family while every other list keeps the UI defaults.
 *
 *  @param r           Renderer; receives two DrawText commands (cursor, then label).
 *  @param style       Supplies the requested family, font_size_body, and colours.
 *  @param role        Font role for the row.
 *  @param label_style Style the label is drawn in (e.g. Bold for a title).
 *  @param label       Option text; drawn verbatim (no wrapping).
 *  @param pos         Top-left where the cursor starts.
 *  @param selected    True → style.selected; false → style.choice.
 *  @param show_cursor Pass false for continuation lines of a wrapped option.
 */
void draw_option(platform::Renderer &r, const PanelStyle &style, FontRole role, FontStyle label_style,
                 std::string_view label, core::math::Vec2 pos, bool selected, bool show_cursor = true);

/** Rendered width of `runs` at `char_size`, each measured in its own style. */
[[nodiscard]] float measure_styled(const platform::Renderer &r, const FontFamily &family,
                                   std::span<const StyledRun> runs, unsigned char_size);

/** Rendered width of `segments` at `char_size`, each measured in its own style. */
[[nodiscard]] float measure_styled(const platform::Renderer &r, const FontFamily &family,
                                   std::span<const StyledSegment> segments, unsigned char_size);

/** Draws `runs` left-to-right from `pos`, advancing x by each run's measured width. */
void draw_styled(platform::Renderer &r, const FontFamily &family, std::span<const StyledRun> runs, unsigned char_size,
                 core::math::Colour colour, core::math::Vec2 pos);

/** Draws `segments` left-to-right from `pos`. `segment.x` is ignored in favour of the
 *  accumulated measured width, so the layout matches wrap_styled().
 */
void draw_styled(platform::Renderer &r, const FontFamily &family, std::span<const StyledSegment> segments,
                 unsigned char_size, core::math::Colour colour, core::math::Vec2 pos);

} // namespace corundum::ui
