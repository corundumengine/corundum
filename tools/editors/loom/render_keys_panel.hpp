// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "editor_state.hpp"

namespace tools::loom {

  /** @brief Render a collapsible list of every flag key the current document references.
   *
   * Condition identifiers come from CompiledExpr::refs(); action strings are
   * parsed for the StateAction LHS. `local.`-prefixed keys are zone-scoped and
   * tinted green; bare keys are global and persistent.
   */
  void render_keys_panel(const EditorState &state);

} // namespace tools::loom