#include "render_graph.hpp"
#include "layout.hpp"

#include <cmath>
#include <imgui.h>

namespace tools::talesmith {

  namespace {

    ImU32 node_color(corundum::gameplay::dialogue::NodeType t) {
      switch (t) {
      case corundum::gameplay::dialogue::NodeType::Talk:
        return IM_COL32(60, 120, 200, 200);
      case corundum::gameplay::dialogue::NodeType::Choice:
        return IM_COL32(200, 160, 60, 200);
      case corundum::gameplay::dialogue::NodeType::Event:
        return IM_COL32(120, 80, 200, 200);
      case corundum::gameplay::dialogue::NodeType::End:
        return IM_COL32(200, 60, 60, 200);
      default:
        return IM_COL32(100, 100, 100, 200);
      }
    }

    ImU32 node_border_color(corundum::gameplay::dialogue::NodeType t) {
      switch (t) {
      case corundum::gameplay::dialogue::NodeType::Talk:
        return IM_COL32(120, 180, 255, 255);
      case corundum::gameplay::dialogue::NodeType::Choice:
        return IM_COL32(255, 200, 80, 255);
      case corundum::gameplay::dialogue::NodeType::Event:
        return IM_COL32(180, 130, 255, 255);
      case corundum::gameplay::dialogue::NodeType::End:
        return IM_COL32(255, 100, 100, 255);
      default:
        return IM_COL32(180, 180, 180, 255);
      }
    }

    const char *node_type_short(corundum::gameplay::dialogue::NodeType t) {
      switch (t) {
      case corundum::gameplay::dialogue::NodeType::Talk:
        return "[Talk]";
      case corundum::gameplay::dialogue::NodeType::Choice:
        return "[Choice]";
      case corundum::gameplay::dialogue::NodeType::Event:
        return "[Event]";
      case corundum::gameplay::dialogue::NodeType::End:
        return "[End]";
      default:
        return "[?]";
      }
    }

    void draw_arrow(ImDrawList *dl, ImVec2 from, ImVec2 to, ImU32 col) {
      const auto dir = ImVec2(to.x - from.x, to.y - from.y);
      const auto len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
      if (len < 1.f)
        return;
      const auto ndir = ImVec2(dir.x / len, dir.y / len);
      const auto perp = ImVec2(-ndir.y, ndir.x);
      const auto arrow_sz = 8.f;
      const auto tip = ImVec2(to.x - ndir.x * (NODE_H * 0.5f + 2.f), to.y - ndir.y * (NODE_H * 0.5f + 2.f));
      const auto base = ImVec2(from.x + ndir.x * (NODE_H * 0.5f + 2.f), from.y + ndir.y * (NODE_H * 0.5f + 2.f));

      dl->AddLine(base, tip, col, 2.f);
      const auto a1 = ImVec2(tip.x - ndir.x * arrow_sz + perp.x * arrow_sz * 0.5f,
                             tip.y - ndir.y * arrow_sz + perp.y * arrow_sz * 0.5f);
      const auto a2 = ImVec2(tip.x - ndir.x * arrow_sz - perp.x * arrow_sz * 0.5f,
                             tip.y - ndir.y * arrow_sz - perp.y * arrow_sz * 0.5f);
      dl->AddTriangleFilled(tip, a1, a2, col);
    }

  } // namespace

  void render_graph(EditorState &state) {
    ImGui::SameLine();
    const auto avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##graph_panel", {avail.x, avail.y}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar);

    const auto cursor = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_origin = {cursor.x, cursor.y};
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvas_origin, {canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y}, true);

    const auto n = static_cast<int>(state.graph.nodes.size());
    if (n == 0) {
      const auto cx = canvas_origin.x + canvas_size.x * 0.5f;
      const auto cy = canvas_origin.y + canvas_size.y * 0.5f;
      dl->AddText({cx - 100.f, cy}, IM_COL32(150, 150, 150, 255), "No nodes. Click + Add Node to begin.");
    }

    // Draw edges
    for (int i = 0; i < n; ++i) {
      const auto &node = state.graph.nodes[i];
      const auto &nl = state.layout[i];
      const auto from = ImVec2(canvas_origin.x + nl.x + NODE_W * 0.5f, canvas_origin.y + nl.y + NODE_H);

      if (node.type == corundum::gameplay::dialogue::NodeType::Talk ||
          node.type == corundum::gameplay::dialogue::NodeType::Event) {
        if (!node.next_id.empty() && node.next_id != "end" && state.graph.id_to_index.contains(node.next_id)) {
          const auto tgt = static_cast<int>(state.graph.id_to_index.at(node.next_id));
          draw_arrow(dl, from,
                     {canvas_origin.x + state.layout[tgt].x + NODE_W * 0.5f, canvas_origin.y + state.layout[tgt].y},
                     IM_COL32(180, 180, 200, 180));
        }
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Choice) {
        for (const auto &ch : node.choices) {
          if (state.graph.id_to_index.contains(ch.target_id)) {
            const auto tgt = static_cast<int>(state.graph.id_to_index.at(ch.target_id));
            draw_arrow(dl, from,
                       {canvas_origin.x + state.layout[tgt].x + NODE_W * 0.5f, canvas_origin.y + state.layout[tgt].y},
                       IM_COL32(200, 170, 100, 150));
          }
        }
      }
    }

    // Draw nodes
    for (int i = 0; i < n; ++i) {
      const auto &node = state.graph.nodes[i];
      const auto &nl = state.layout[i];
      const auto pos = ImVec2(canvas_origin.x + nl.x, canvas_origin.y + nl.y);
      const auto sz = ImVec2(NODE_W, NODE_H);

      const bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x + sz.x, pos.y + sz.y});
      const bool selected = (i == state.selected_node);

      if (hovered || selected) {
        dl->AddRectFilled(pos, {pos.x + sz.x, pos.y + sz.y}, IM_COL32(40, 40, 55, 240));
        dl->AddRect(pos, {pos.x + sz.x, pos.y + sz.y}, node_border_color(node.type), 4.f, 0, 2.f);
      } else {
        dl->AddRectFilled(pos, {pos.x + sz.x, pos.y + sz.y}, node_color(node.type));
        dl->AddRect(pos, {pos.x + sz.x, pos.y + sz.y}, node_border_color(node.type), 2.f);
      }

      auto title = node.id;
      if (title.size() > 28)
        title = title.substr(0, 25) + "...";

      dl->AddText({pos.x + 6.f, pos.y + 3.f}, IM_COL32(255, 255, 255, 255), title.c_str());
      dl->AddText({pos.x + 6.f, pos.y + 20.f}, IM_COL32(200, 200, 200, 255), node_type_short(node.type));

      if (node.type == corundum::gameplay::dialogue::NodeType::Talk) {
        auto preview = node.text.substr(0, 30);
        if (!preview.empty())
          dl->AddText({pos.x + 6.f, pos.y + 38.f}, IM_COL32(160, 160, 180, 255), preview.c_str());
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Choice) {
        const auto count = node.choices.size();
        const auto cc = std::to_string(count) + " choice" + (count != 1 ? "s" : "");
        dl->AddText({pos.x + 6.f, pos.y + 38.f}, IM_COL32(160, 160, 180, 255), cc.c_str());
      }

      if (hovered && !state.inspector_open && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.selected_node = i;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          state.inspector_open = true;
      }
    }

    if (state.selected_node >= 0 && n > 0) {
      const auto &nl = state.layout[state.selected_node];
      const auto pos = ImVec2(canvas_origin.x + nl.x, canvas_origin.y + nl.y);
      ImGui::SetScrollHereX(0.5f);
      ImGui::SetScrollHereY(0.5f);
    }

    dl->PopClipRect();
    ImGui::EndChild();
  }

} // namespace tools::talesmith
