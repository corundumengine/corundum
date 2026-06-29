#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>

#include <cstddef>
#include <imgui.h>

namespace tools::talesmith {

  using corundum::gameplay::dialogue::NodeType;

  struct NodeTypeTraits {
    const char *label;
    const char *short_label;
    ImU32 fill_color;
    ImU32 border_color;
    ImU32 list_color;
  };

  constexpr NodeTypeTraits kNodeTypeTraits[] = {
      /* Talk   */ {"Talk", "[Talk]", IM_COL32(60, 120, 200, 200), IM_COL32(120, 180, 255, 255),
                    IM_COL32(100, 180, 255, 255)},
      /* Choice */
      {"Choice", "[Choice]", IM_COL32(200, 160, 60, 200), IM_COL32(255, 200, 80, 255), IM_COL32(255, 200, 80, 255)},
      /* Event  */
      {"Event", "[Event]", IM_COL32(120, 80, 200, 200), IM_COL32(180, 130, 255, 255), IM_COL32(160, 120, 255, 255)},
      /* End    */
      {"End", "[End]", IM_COL32(200, 60, 60, 200), IM_COL32(255, 100, 100, 255), IM_COL32(255, 100, 100, 255)},
  };

  static_assert(std::size(kNodeTypeTraits) == 4,
                "kNodeTypeTraits must have one entry per NodeType enum value (Talk=0, Choice=1, Event=2, End=3)");

  constexpr const NodeTypeTraits &traits_for(NodeType t) noexcept {
    return kNodeTypeTraits[static_cast<int>(t)];
  }

  constexpr const char *node_label(NodeType t) noexcept {
    return traits_for(t).label;
  }

  constexpr const char *node_short_label(NodeType t) noexcept {
    return traits_for(t).short_label;
  }

  constexpr ImU32 node_fill_color(NodeType t) noexcept {
    return traits_for(t).fill_color;
  }

  constexpr ImU32 node_border_color(NodeType t) noexcept {
    return traits_for(t).border_color;
  }

  constexpr ImU32 node_list_color(NodeType t) noexcept {
    return traits_for(t).list_color;
  }

} // namespace tools::talesmith
