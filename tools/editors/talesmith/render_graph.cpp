#include "render_graph.hpp"
#include "layout.hpp"
#include "node_type_traits.hpp"

#include <cmath>
#include <imgui.h>

namespace tools::talesmith {

  namespace {

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
    ImGui::BeginChild("##graph_panel", {state.graph_width_, avail.y}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar);

    const auto cursor = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_origin = {cursor.x, cursor.y};
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvas_origin, {canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y}, true);

    // ── Canvas pan/zoom ──
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        auto delta = ImGui::GetIO().MouseDelta;
        state.canvas.offset_x += delta.x;
        state.canvas.offset_y += delta.y;
      }
      float wheel = ImGui::GetIO().MouseWheel;
      if (wheel != 0.f) {
        float old_scale = state.canvas.scale;
        state.canvas.scale = std::clamp(state.canvas.scale * (1.f + wheel * 0.1f), CanvasTransform::k_min_scale,
                                        CanvasTransform::k_max_scale);
        ImVec2 mouse_pos = ImGui::GetMousePos();
        state.canvas.offset_x =
            mouse_pos.x - canvas_origin.x -
            (mouse_pos.x - canvas_origin.x - state.canvas.offset_x) * (state.canvas.scale / old_scale);
        state.canvas.offset_y =
            mouse_pos.y - canvas_origin.y -
            (mouse_pos.y - canvas_origin.y - state.canvas.offset_y) * (state.canvas.scale / old_scale);
      }
    }

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
      const auto from = ImVec2(
          canvas_origin.x + state.canvas.offset_x + nl.x * state.canvas.scale + NODE_W * state.canvas.scale * 0.5f,
          canvas_origin.y + state.canvas.offset_y + nl.y * state.canvas.scale + NODE_H * state.canvas.scale);

      if (node.type == corundum::gameplay::dialogue::NodeType::Talk ||
          node.type == corundum::gameplay::dialogue::NodeType::Event) {
        if (!node.next_id.empty() && node.next_id != "end" && state.graph.id_to_index.contains(node.next_id)) {
          const auto tgt = static_cast<int>(state.graph.id_to_index.at(node.next_id));
          draw_arrow(dl, from,
                     {canvas_origin.x + state.canvas.offset_x + state.layout[tgt].x * state.canvas.scale +
                          NODE_W * state.canvas.scale * 0.5f,
                      canvas_origin.y + state.canvas.offset_y + state.layout[tgt].y * state.canvas.scale},
                     IM_COL32(180, 180, 200, 180));
        }
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Choice) {
        for (const auto &ch : node.choices) {
          if (state.graph.id_to_index.contains(ch.target_id)) {
            const auto tgt = static_cast<int>(state.graph.id_to_index.at(ch.target_id));
            draw_arrow(dl, from,
                       {canvas_origin.x + state.canvas.offset_x + state.layout[tgt].x * state.canvas.scale +
                            NODE_W * state.canvas.scale * 0.5f,
                        canvas_origin.y + state.canvas.offset_y + state.layout[tgt].y * state.canvas.scale},
                       IM_COL32(200, 170, 100, 150));
          }
        }
      }
    }

    // Draw nodes
    for (int i = 0; i < n; ++i) {
      const auto &node = state.graph.nodes[i];
      const auto &nl = state.layout[i];
      const auto pos = ImVec2(canvas_origin.x + state.canvas.offset_x + nl.x * state.canvas.scale,
                              canvas_origin.y + state.canvas.offset_y + nl.y * state.canvas.scale);
      const auto sz = ImVec2(NODE_W * state.canvas.scale, NODE_H * state.canvas.scale);

      const bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x + sz.x, pos.y + sz.y});
      const bool selected = (i == state.selected_node);

      if (hovered || selected) {
        dl->AddRectFilled(pos, {pos.x + sz.x, pos.y + sz.y}, IM_COL32(40, 40, 55, 240));
        dl->AddRect(pos, {pos.x + sz.x, pos.y + sz.y}, node_border_color(node.type), 4.f, 0, 2.f);
      } else {
        dl->AddRectFilled(pos, {pos.x + sz.x, pos.y + sz.y}, node_fill_color(node.type));
        dl->AddRect(pos, {pos.x + sz.x, pos.y + sz.y}, node_border_color(node.type), 2.f);
      }

      auto title = node.id;
      if (title.size() > 28)
        title = title.substr(0, 25) + "...";

      dl->AddText({pos.x + 6.f, pos.y + 3.f}, IM_COL32(255, 255, 255, 255), title.c_str());
      dl->AddText({pos.x + 6.f, pos.y + 20.f}, IM_COL32(200, 200, 200, 255), node_short_label(node.type));

      if (node.type == corundum::gameplay::dialogue::NodeType::Talk) {
        auto preview = node.text.substr(0, 30);
        if (!preview.empty())
          dl->AddText({pos.x + 6.f, pos.y + 38.f}, IM_COL32(160, 160, 180, 255), preview.c_str());
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Choice) {
        const auto count = node.choices.size();
        const auto cc = std::to_string(count) + " choice" + (count != 1 ? "s" : "");
        dl->AddText({pos.x + 6.f, pos.y + 38.f}, IM_COL32(160, 160, 180, 255), cc.c_str());
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.selected_node = i;
        state.inspector_open = true;
      }
    }

    if (state.selected_node >= 0 && n > 0 && state.selected_node != state.last_scroll_target_) {
      state.last_scroll_target_ = state.selected_node;
      ImGui::SetScrollHereX(0.5f);
      ImGui::SetScrollHereY(0.5f);
    }

    dl->PopClipRect();
    ImGui::EndChild();
  }

} // namespace tools::talesmith
