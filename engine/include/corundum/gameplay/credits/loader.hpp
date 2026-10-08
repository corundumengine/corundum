// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/credits/credits.hpp>

#include <expected>
#include <filesystem>
#include <string>

namespace corundum::gameplay::credits {

  /** @brief Load a credits file: `{ "title": "...", "sections": [ { "heading": "...", "lines": ["..."] } ] }`.
   *
   *  A one-shot, side-effect-scoped read (free function, no state), so the Credits screen can
   *  hide itself when the file is absent or malformed rather than opening in a broken state.
   *
   *  @param path Credits JSON file.
   *  @return The credits file (title and sections), or an error describing why the file could
   *          not be read or parsed.
   */
  [[nodiscard]] std::expected<CreditsFile, std::string> load_credits_file(const std::filesystem::path &path);

} // namespace corundum::gameplay::credits
