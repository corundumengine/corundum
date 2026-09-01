#pragma once
#include "editor_state.hpp"

namespace tools::tilesmith {

  /// @brief Records the current state.map/state.portals/state.active_layer as a new undo checkpoint.
  ///
  /// Call this AFTER a mutation completes (once per logical edit — one paint stroke, one fill, one
  /// layer add, etc.), never before or mid-edit: corundum::tool_host::UndoStack<Doc>'s
  /// push()/undo()/redo() treat states_[cursor_] as "the current document," so each push must capture
  /// the document as it exists right now, not the state about to be superseded.
  void push_undo_checkpoint(EditorState &state);

  /// @brief Steps state.map/state.portals/state.active_layer back to the previous checkpoint.
  /// @return false (no-op) if there is nothing to undo.
  bool apply_undo(EditorState &state);

  /// @brief Steps state.map/state.portals/state.active_layer forward to the next checkpoint.
  /// @return false (no-op) if there is nothing to redo.
  bool apply_redo(EditorState &state);

} // namespace tools::tilesmith
