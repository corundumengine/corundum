#pragma once

#include "editor_state.hpp"

namespace tools::talesmith {

  void recompute_layout(corundum::gameplay::dialogue::Graph &graph, std::vector<NodeLayout> &layout, float graph_width);

} // namespace tools::talesmith
