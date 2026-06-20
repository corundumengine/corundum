#include <corundum/gameplay/ui/dialog_box.hpp>

#include <utility>

namespace corundum::gameplay::ui {

  void dialog_box_update(DialogBoxState &ds, const gameplay::dialogue::State &state, const gameplay::FlagStore &flags,
                         platform::Renderer &r, core::math::Vec2 viewport) {
    if (!state.active || !state.graph) {
      ds.visible = false;
      return;
    }

    const float panel_w = viewport.x - ds.style.margin * 2.f;
    const bool stale = !ds.layout || state.current_id != ds.last_node_id || panel_w != ds.last_panel_w;

    if (stale) {
      auto measure = [&](std::string_view text) -> float {
        return r.measure_text(ds.style.font_id, text, ds.style.font_size_body);
      };

      ds.layout =
          build_layout(state, flags, ds.style.margin, ds.style.panel_height_frac, ds.border.tile_w, viewport, measure);
      ds.last_node_id = state.current_id;
      ds.last_panel_w = panel_w;
    } else {
      ds.layout->selected_choice = state.selected_choice;
    }

    ds.visible = true;
  }

  void dialog_box_render(const DialogBoxState &ds, platform::Renderer &r) {
    if (!ds.visible || !ds.layout)
      return;

    const DialogLayout &lay = *ds.layout;
    const float px = lay.panel_pos.x;
    const float py = lay.panel_pos.y;
    const float pw = lay.panel_size.x;
    const float ph = lay.panel_size.y;
    const float inset = lay.inset;
    const float spacing = ds.style.line_spacing;

    r.draw(platform::DrawRect{.position = lay.panel_pos, .size = lay.panel_size, .colour = ds.style.bg});

    ds.border.render(r, px, py, pw, ph);

    auto draw_str = [&](std::string_view text, unsigned size, core::math::Colour col, float x, float y) {
      if (!text.empty())
        r.draw(platform::DrawText{ds.style.font_id, text, {x, y}, size, col});
    };

    switch (lay.node_type) {
    case gameplay::dialogue::NodeType::Talk: {
      draw_str(lay.speaker, ds.style.font_size_speaker, ds.style.speaker, px + inset, py + inset);
      float y = py + inset + spacing;
      for (const auto &line : lay.body_lines) {
        if (line.empty()) {
          y += spacing;
          continue;
        }
        draw_str(line, ds.style.font_size_body, ds.style.body, px + inset, y);
        y += spacing;
      }
      draw_str("[Select] Continue   [Cancel] Close", ds.style.font_size_prompt, ds.style.choice, px + inset,
               y + spacing / 2.f);
      break;
    }
    case gameplay::dialogue::NodeType::Choice: {
      draw_str("Choose:", ds.style.font_size_speaker, ds.style.speaker, px + inset, py + inset);
      for (std::size_t i = 0; i < lay.choice_lines.size(); ++i) {
        const bool is_sel = (static_cast<int>(i) == lay.selected_choice);
        const std::string label = (is_sel ? "> " : "  ") + lay.choice_lines[i];
        const float y = py + inset + spacing * (1.f + static_cast<float>(i));
        draw_str(label, ds.style.font_size_body, is_sel ? ds.style.selected : ds.style.choice, px + inset, y);
      }
      break;
    }
    case gameplay::dialogue::NodeType::End:
      draw_str("[Select] Close", ds.style.font_size_body, ds.style.choice, px + inset, py + inset);
      break;
    case gameplay::dialogue::NodeType::Event:
      break;
    default:
      std::unreachable();
    }
  }

} // namespace corundum::gameplay::ui
