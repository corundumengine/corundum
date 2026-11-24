// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::codex {

/** Current on-disk codex batch file format version; every batch file must declare it. */
inline constexpr int k_codex_schema_version = 1;

/** FlagStore key prefix marking a codex entry as unlocked: `codex.<id>`. */
inline constexpr std::string_view k_flag_prefix = "codex.";

/** FlagStore key that unlocks `id` (`codex.<id>`). */
[[nodiscard]] inline std::string flag_key(std::string_view id) {
  return std::format("{}{}", k_flag_prefix, id);
}

/** True when `flag_key` names a codex entry's unlock flag. */
[[nodiscard]] constexpr bool is_codex_flag(std::string_view flag_key) noexcept {
  return flag_key.starts_with(k_flag_prefix);
}

/** Strip the `codex.` prefix from `flag_key`.
 *  @pre is_codex_flag(flag_key) returned true for `flag_key`.
 */
[[nodiscard]] constexpr std::string_view entry_id_from_flag(std::string_view flag_key) noexcept {
  flag_key.remove_prefix(k_flag_prefix.size());
  return flag_key;
}

/** A static lore entry loaded from data/codex/<file>.json.
 *
 *  The entry is hidden from the journal until its `codex.<id>` flag is set — by a
 *  `set_flag` dialogue action, an `unlock_codex` event, or `starting_flags`.
 */
struct CodexEntry {
  std::string body{};     ///< Long-form lore text shown in the codex detail pane.
  std::string category{}; ///< Author-defined grouping label, e.g. "Places"; empty groups under "Lore".
  std::string id{};       ///< Unique key; also the suffix of the unlock flag.
  std::string title{};    ///< Display heading.
};

} // namespace corundum::gameplay::codex
