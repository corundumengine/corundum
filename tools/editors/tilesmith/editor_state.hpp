#pragma once
#include "new_map_dialog.hpp"
#include "portal_entry.hpp"
#include <corundum/tool_host/canvas_controller.hpp>
#include <corundum/tool_host/file_browser.hpp>
#include <corundum/tool_host/undo.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace tools::tilesmith {

  /// @brief One undo/redo checkpoint: everything that gets saved to the tilemap JSON plus the
  /// portals sidecar. Deliberately does NOT include active_layer — which layer is selected is
  /// live UI state (like selected_gid or the camera), not document content; bundling it here
  /// caused undo to snap the active layer back to whatever it was at the *previous* checkpoint
  /// instead of leaving the user's current layer selection alone. See adopt_doc() in undo.cpp
  /// for how the active layer is instead just clamped into range after a restore.
  struct TilemapDoc {
    corundum::world::tilemap::Tilemap map;
    std::vector<PortalEntry> portals;
  };

  /**
   * @brief All mutable state for a tilesmith editing session.
   *
   * Pure data — no GLFW, no I/O, no algorithms. Every subsystem receives a
   * reference to this struct and reads/writes only the fields it owns.
   */
  struct EditorState {
    corundum::world::tilemap::Tilemap map; ///< Loaded tilemap (edited in place).
    std::filesystem::path map_path;        ///< Source file path used for save.

    int active_layer = 0;                              ///< Index of the currently selected layer.
    corundum::world::tilemap::TileId selected_gid = 0; ///< GID of the tile selected in the palette.
    uint8_t selected_flip = 0;                         ///< k_flip_h | k_flip_v bitmask for the next paint operation.
    int palette_tileset_idx = 0;                       ///< Index of the active tileset tab.
    float palette_scroll_y = 0.f;                      ///< Vertical scroll offset in pixels within the palette's flow
                                                       ///< layout (tiles have no uniform size, so there's no fixed row
                                                       ///< height to scroll by — see compute_palette_layout()).
    int palette_tabbar_h = 28;                         ///< Measured ImGui tab bar height (updated each frame).

    corundum::tool_host::CanvasController canvas;
    /// User-controlled zoom for the palette panel (independent of canvas zoom); 1 = native pixel
    /// size. Not auto-fit to any particular tile — packed tilesets can mix wildly different native
    /// sizes, so picking one "target" tile to fit would shrink or blow up everything else. Adjust
    /// with Ctrl+scroll over the palette (see input.cpp); clamped to [k_palette_min_scale,
    /// k_palette_max_scale].
    float palette_tile_scale = 1.f;

    bool dirty = false;               ///< True when unsaved changes exist.
    bool show_grid = true;            ///< Whether to draw the isometric grid overlay.
    bool show_collisions = false;     ///< Whether to display/edit collision shapes.
    bool show_elevation = false;      ///< Whether to display/edit per-tile elevation.
    uint8_t selected_elevation = 0;   ///< Brush value for the next elevation paint operation, clamped [0,100].
    float elev_step_px = 4.f;         ///< Pixels per elevation unit, mirrored from GameConfig::elevation_step_px.
    bool show_walkability = false;    ///< Whether to overlay disconnected walkability edges.
    unsigned int max_step_height = 4; ///< Mirrored from GameConfig::max_step_height; read-only in Tilesmith.
    bool show_ramps = false;          ///< Whether to display/edit ramp tiles.
    corundum::world::tilemap::RampAxis selected_ramp_axis =
        corundum::world::tilemap::RampAxis::NorthSouth; ///< Axis for the next ramp paint operation.

    /// When show_collisions is true, controls whether left-click places triangle or rect.
    bool triangle_collision_mode = false;
    /// Orientation of the next triangle to place (cycles with [ / ] keys).
    corundum::world::tilemap::TriangleCut collision_tri_cut = corundum::world::tilemap::TriangleCut::NorthWest;

    // Collision drag state
    bool collision_dragging = false; ///< True while a collision rect drag is in progress.
    int col_drag_anchor_col = 0;     ///< Tile column where the drag began.
    int col_drag_anchor_row = 0;     ///< Tile row where the drag began.
    int col_drag_cur_col = 0;        ///< Current tile column under the cursor.
    int col_drag_cur_row = 0;        ///< Current tile row under the cursor.
    bool col_drag_sub_tile = false;  ///< True when drag was started with Shift held (pixel precision).
    int col_drag_anchor_win_x = 0;   ///< Window-space X at drag start (used for sub-tile mode).
    int col_drag_anchor_win_y = 0;   ///< Window-space Y at drag start (used for sub-tile mode).
    int col_drag_cur_win_x = 0;      ///< Window-space X at current cursor position.
    int col_drag_cur_win_y = 0;      ///< Window-space Y at current cursor position.

    // Hovered tile (updated every mouse move; -1 when outside the canvas or map)
    int hover_tile_col = -1; ///< Tile column currently under the cursor.
    int hover_tile_row = -1; ///< Tile row currently under the cursor.

    // Erase drag state
    bool erase_dragging = false;   ///< True while a rect-erase drag is in progress.
    int erase_drag_anchor_col = 0; ///< Tile column where the erase drag began.
    int erase_drag_anchor_row = 0; ///< Tile row where the erase drag began.
    int erase_drag_cur_col = 0;    ///< Current tile column under the cursor during erase drag.
    int erase_drag_cur_row = 0;    ///< Current tile row where the cursor during erase drag.

    // Paint drag state
    bool painting_active = false; ///< True while a left-click paint/erase-mode drag is in progress.

    corundum::tool_host::UndoStack<TilemapDoc> undo; ///< General undo/redo — see undo.hpp.

    // Portal state
    std::vector<PortalEntry> portals; ///< Portals loaded from data/portals/{stem}.json.
    bool show_portals = false;        ///< Whether to display/edit portal rectangles.
    bool show_portals_popup = false;  ///< Whether to display the portal edit popup.
    int selected_portal = -1;         ///< Index of the currently selected portal, or -1.

    // Portal drag state
    bool portal_dragging = false;   ///< True while a portal rect drag is in progress.
    int portal_drag_anchor_col = 0; ///< Tile column where the portal drag began.
    int portal_drag_anchor_row = 0; ///< Tile row where the portal drag began.
    int portal_drag_cur_col = 0;    ///< Current tile column under the cursor during portal drag.
    int portal_drag_cur_row = 0;    ///< Current tile row under the cursor during portal drag.

    // Validation-errors popup (shown by any save path — Ctrl+S, File > Save, Save As, Save & Exit)
    bool show_validation_popup = false;
    std::vector<std::string> validation_errors;

    // Menu bar state (File > Open/Save As browsers, New Map dialog, exit/open confirmation)
    corundum::tool_host::FileBrowserState open_browser;
    corundum::tool_host::FileBrowserState save_browser;
    bool show_exit_confirm = false;
    bool show_open_confirm = false;
    std::filesystem::path pending_open_path;
    bool show_new_map_dialog = false;
    bool new_map_requested = false;
    NewMapDialogState new_map_dlg;

    // Last save/open failure, if any — shown by the popup in menu.cpp instead of stderr-only.
    std::string last_io_error;
    bool show_io_error_popup = false;
  };

} // namespace tools::tilesmith
