// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>

#include <string>
#include <string_view>

namespace corundum::test {

  /// Point every role's regular face at @p name and leave bold/italic unset. The null backend
  /// ignores file existence, so tests can use a name that does not resolve to a real file.
  inline void set_missing_fonts(corundum::core::ResourcePaths &paths, std::string_view name = "missing.ttf") {
    for (auto &family : paths.fonts)
      family.regular = std::string{name};
  }

} // namespace corundum::test
