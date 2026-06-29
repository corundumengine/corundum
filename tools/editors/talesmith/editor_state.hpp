#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace tools::talesmith {

  struct CanvasTransform {
    float offset_x = 0.f;
    float offset_y = 0.f;
    float scale = 1.f;

    static constexpr float k_min_scale = 0.25f;
    static constexpr float k_max_scale = 2.0f;
  };

  struct NodeLayout {
    int layer = 0;
    float x = 0.f;
    float y = 0.f;
  };

  struct PopupState {
    bool show_save_as = false;
    char save_as_path_buf[512]{};
    bool show_open = false;
    char open_path_buf[512]{};
    bool show_close_confirm = false;
  };

  struct InspectorState {
    char id_buf[128]{};
    char text_buf[1024]{};
    char next_id_buf[128]{};
    char cond_buf[256]{};
    char action_buf[256]{};
    char label_buf[256]{};
    char target_buf[128]{};
    char meta_key_buf[128]{};
    char meta_val_buf[256]{};

    void clear() {
      std::memset(id_buf, 0, sizeof(id_buf));
      std::memset(text_buf, 0, sizeof(text_buf));
      std::memset(next_id_buf, 0, sizeof(next_id_buf));
      std::memset(cond_buf, 0, sizeof(cond_buf));
      std::memset(action_buf, 0, sizeof(action_buf));
      std::memset(label_buf, 0, sizeof(label_buf));
      std::memset(target_buf, 0, sizeof(target_buf));
      std::memset(meta_key_buf, 0, sizeof(meta_key_buf));
      std::memset(meta_val_buf, 0, sizeof(meta_val_buf));
    }
  };

  struct GraphSnapshot {
    corundum::gameplay::dialogue::Graph graph;
    int selected_node = -1;
  };

  struct UndoStack {
    static constexpr int k_max_depth = 256;
    std::vector<GraphSnapshot> states_;
    int cursor_ = -1;

    void push(const GraphSnapshot &snap) {
      if (cursor_ >= 0 && cursor_ < static_cast<int>(states_.size()) - 1)
        states_.resize(static_cast<std::size_t>(cursor_ + 1));
      if (static_cast<int>(states_.size()) >= k_max_depth)
        states_.erase(states_.begin());
      states_.push_back(snap);
      cursor_ = static_cast<int>(states_.size()) - 1;
    }

    bool can_undo() const noexcept {
      return cursor_ > 0;
    }

    bool undo(GraphSnapshot &out) noexcept {
      if (!can_undo())
        return false;
      --cursor_;
      out = states_[static_cast<std::size_t>(cursor_)];
      return true;
    }

    bool can_redo() const noexcept {
      return cursor_ >= 0 && cursor_ < static_cast<int>(states_.size()) - 1;
    }

    bool redo(GraphSnapshot &out) noexcept {
      if (!can_redo())
        return false;
      ++cursor_;
      out = states_[static_cast<std::size_t>(cursor_)];
      return true;
    }

    void clear() {
      states_.clear();
      cursor_ = -1;
    }
  };

  struct ToastState {
    std::string message;
    int frames_remaining = 0;

    void show(std::string msg) {
      message = std::move(msg);
      frames_remaining = 180;
    }
  };

  struct EditorState {
    std::filesystem::path file_path;
    bool dirty = false;

    corundum::gameplay::dialogue::Graph graph;

    int selected_node = -1;
    bool inspector_open = false;

    int last_scroll_target_ = -1;

    bool add_node_open = false;
    int add_node_type = 0;
    char add_node_id_buf[64] = {};

    std::vector<NodeLayout> layout;
    char graph_speaker_buf_[256]{};
    char graph_id_buf_[128]{};
    CanvasTransform canvas;
    PopupState popups;
    InspectorState inspector_bufs;
    ToastState toast;
    UndoStack undo_stack;

    float node_list_width_ = 260.f;
    float inspector_width_ = 380.f;
    float graph_width_ = 1340.f;

    void push_undo_snapshot() {
      GraphSnapshot snap;
      snap.graph = graph;
      snap.selected_node = selected_node;
      undo_stack.push(snap);
    }
  };

} // namespace tools::talesmith
