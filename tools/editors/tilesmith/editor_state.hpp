#pragma once
#include "portal_entry.hpp"
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <filesystem>
#include <vector>

namespace tools::tilemap {

  /**
   * @brief All mutable state for a tilesmith editing session.
   *
   * Pure data — no GLFW, no I/O, no algorithms. Every subsystem receives a
   * reference to this struct and reads/writes only the fields it owns.
   */
  struct EditorState {
    corundum::gameplay::world::tilemap::Tilemap map; ///< Loaded tilemap (edited in place).
    std::filesystem::path map_path;                  ///< Source file path used for save.

    int active_layer = 0;                                        ///< Index of the currently selected layer.
    corundum::gameplay::world::tilemap::TileId selected_gid = 0; ///< GID of the tile selected in the palette.
    uint8_t selected_flip = 0;   ///< k_flip_h | k_flip_v bitmask for the next paint operation.
    int palette_tileset_idx = 0; ///< Index of the active tileset tab.
    int palette_scroll_row = 0;  ///< First visible row in the palette tile grid.
    int palette_scroll_col = 0;  ///< First visible column in the palette tile grid.
    int palette_tabbar_h = 28;   ///< Measured ImGui tab bar height (updated each frame).

    corundum::gameplay::world::Camera camera; ///< Viewport scroll position in world pixels.
    float tile_scale = 2.f;                   ///< Canvas render scale factor (1 = native, 2 = 2x, etc.).
    float palette_tile_scale = 2.f; ///< Scale used to display tiles in the palette panel (independent of canvas zoom).

    bool dirty = false;               ///< True when unsaved changes exist.
    bool show_grid = true;            ///< Whether to draw the isometric grid overlay.
    bool show_collisions = false;     ///< Whether to display/edit collision shapes.
    bool show_elevation = false;      ///< Whether to display/edit per-tile elevation.
    uint8_t selected_elevation = 0;   ///< Brush value for the next elevation paint operation, clamped [0,100].
    float elev_step_px = 4.f;         ///< Pixels per elevation unit, mirrored from GameConfig::elevation_step_px.
    bool show_walkability = false;    ///< Whether to overlay disconnected walkability edges.
    unsigned int max_step_height = 4; ///< Mirrored from GameConfig::max_step_height; read-only in Tilesmith.

    /// When show_collisions is true, controls whether left-click places triangle or rect.
    bool triangle_collision_mode = false;
    /// Orientation of the next triangle to place (cycles with [ / ] keys).
    corundum::gameplay::world::tilemap::TriangleCut collision_tri_cut =
        corundum::gameplay::world::tilemap::TriangleCut::NW;

    bool camera_moved = false; ///< True for one frame after keyboard navigation moves the camera.

    // Middle-mouse pan state
    bool panning = false;                               ///< True while middle-mouse pan is active.
    int pan_anchor_x = 0;                               ///< Window-space pan start X.
    int pan_anchor_y = 0;                               ///< Window-space pan start Y.
    corundum::gameplay::world::Camera pan_start_camera; ///< Camera position at pan start.

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

    // Canvas scrollbar drag state
    bool scrollbar_dragging = false; ///< True from mouse-down on the canvas's own scrollbar until release.

    // Fill undo state (single-action; not a general undo stack)
    std::vector<corundum::gameplay::world::tilemap::TileId>
        fill_undo_tiles;          ///< Pre-fill tiles snapshot, empty when nothing to undo.
    int fill_undo_layer_idx = -1; ///< Layer index the snapshot belongs to, or -1 when nothing to undo.

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
  };

} // namespace tools::tilemap
