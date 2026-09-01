#include "undo.hpp"
#include <algorithm>
#include <utility>

namespace tools::tilesmith {

  void push_undo_checkpoint(EditorState &state) {
    state.undo.push(TilemapDoc{state.map, state.portals});
  }

  namespace {
    void adopt_doc(EditorState &state, TilemapDoc &&doc) {
      // Tileset management (which sprite sheets are loaded) is intentionally outside undo's
      // scope — TilesetView::tileset (owned by main.cpp's tileset_views, rebuilt only by
      // do_add_tileset()/do_remove_tileset()) holds raw pointers into state.map.tilesets's
      // backing storage (see tileset_view.hpp's lifetime comment). Moving the live tilesets
      // vector out before the doc swap and back in afterward keeps that storage's address
      // unchanged, so those pointers stay valid — a plain `state.map = std::move(doc.map)`
      // both silently reverted tilesets to the checkpoint's stale copy AND relocated the
      // vector's backing storage, leaving every TilesetView dangling (use-after-free) until
      // something else happened to call rebuild_tileset_views() again.
      auto tilesets = std::move(state.map.tilesets);
      state.map = std::move(doc.map);
      state.map.tilesets = std::move(tilesets);
      state.portals = std::move(doc.portals);
      // active_layer is intentionally NOT sourced from the checkpoint (see TilemapDoc's comment)
      // — only clamped, so switching layers between edits isn't silently undone along with the
      // edit itself. Only matters if the restored layer count shrank (undoing a layer add) or the
      // current selection otherwise fell out of range.
      state.active_layer =
          std::clamp(state.active_layer, 0, std::max(0, static_cast<int>(state.map.layers.size()) - 1));
      if (state.selected_portal >= static_cast<int>(state.portals.size()))
        state.selected_portal = -1;
      // A structural change may have just happened underneath any in-progress drag; bail out of
      // all of them rather than let a stale drag keep mutating the just-restored document.
      state.painting_active = false;
      state.collision_dragging = false;
      state.erase_dragging = false;
      state.portal_dragging = false;
      state.dirty = true;
    }
  } // namespace

  bool apply_undo(EditorState &state) {
    TilemapDoc doc;
    if (!state.undo.undo(doc))
      return false;
    adopt_doc(state, std::move(doc));
    return true;
  }

  bool apply_redo(EditorState &state) {
    TilemapDoc doc;
    if (!state.undo.redo(doc))
      return false;
    adopt_doc(state, std::move(doc));
    return true;
  }

} // namespace tools::tilesmith
