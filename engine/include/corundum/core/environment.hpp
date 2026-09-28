// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>

namespace corundum::core {

  /**
   * @brief Reads the environment variable @p name.
   *
   * Distinguishes unset from set-but-empty: an unset variable yields
   * std::nullopt, while an empty one yields an empty string.
   *
   * @param name Variable name; a null or empty name is treated as unset.
   * @return The variable's value, or std::nullopt when it is unset.
   */
  [[nodiscard]] std::optional<std::string> read_env(const char *name);

} // namespace corundum::core
