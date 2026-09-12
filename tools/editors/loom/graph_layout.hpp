// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "editor_state.hpp"

namespace tools::loom {

  void recompute_layout(corundum::dialogue::Graph &graph, std::vector<NodeLayout> &layout, float graph_width);

} // namespace tools::loom
