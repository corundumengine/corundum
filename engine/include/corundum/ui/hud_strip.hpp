// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <string_view>

namespace corundum::ui {

  /** @brief FlagStore key holding the player's currency. */
  inline constexpr std::string_view k_gold_flag = "gold";

  /** @brief The persistent one-line HUD's content: gold and the tracked active quest. */
  struct HudStripData {
    int gold{};
    std::string quest_name{};
    std::string objective{};
    bool has_quest{false};
  };

  /** @brief Derive the HUD strip from the flag store and quest registry.
   *
   *  Gold is the `gold` flag. The active quest is the first quest in Active lifecycle order —
   *  a placeholder until the tracked-quest model (`quest.<id>.tracked`) lands; `objective` is
   *  that quest's current-stage first objective.
   *
   *  @param flags   Active FlagStore.
   *  @param quests  Loaded quest registry.
   *  @param zone_id Current zone; `local.<key>` objective conditions resolve against it.
   */
  [[nodiscard]] HudStripData build_hud_strip(const world::FlagStore &flags, const gameplay::quest::Registry &quests,
                                             std::string_view zone_id = {});

  /** @brief Draw the persistent HUD as a small top-left box: gold plus the active objective.
   *
   *  Pure render. The box is drawn as an opaque fill (panel_fill()) plus a nine-patch frame
   *  (panel_frame()) — separate calls so the fill can be opaque, unlike the modal panels' shared
   *  translucent style.bg, which would tint the world showing through the always-on HUD. The
   *  caller skips the HUD while a screen is open.
   *
   *  @param r      Platform renderer; receives one DrawRect, the border's DrawSprite commands,
   *                then one DrawText.
   *  @param style  Dialog text style (font id/size/colours).
   *  @param border Pre-loaded nine-patch border texture/tile dims; same one used by the dialogue box.
   *  @param data   Content derived by build_hud_strip().
   */
  void hud_strip_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                        const HudStripData &data);

} // namespace corundum::ui
