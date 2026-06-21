#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace tools::talesmith {

  struct NodeLayout {
    int layer = 0;
    float x = 0.f;
    float y = 0.f;
  };

  struct EditorState {
    std::filesystem::path file_path;
    bool dirty = false;

    corundum::gameplay::dialogue::Graph graph;

    int selected_node = -1;
    bool inspector_open = false;

    bool add_node_open = false;
    int add_node_type = 0;
    char add_node_id_buf[64] = {};

    std::vector<NodeLayout> layout;
  };

} // namespace tools::talesmith
