#pragma once
#include "editor_state.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"
#include <corundum/tool_host/tool_host.hpp>
#include <vector>

namespace tools::tilesmith {

  /** @brief Renders the top menu bar (File/Edit/View) and handles its actions in place.
   *
   * Including opening/saving a different map, which mutates state/texture_store/tileset_views
   * and can set running = false (Exit). Call once per frame, before the canvas/panel windows.
   *
   * @param host Window/host for title updates and texture loading.
   * @param state Editor state — owns all per-session state (file browsers, dialog state, etc).
   * @param texture_store GPU texture storage, rebuilt by File > Open / File > New Map.
   * @param tileset_views Tileset view data, rebuilt by File > Open / File > New Map.
   * @param running Set to false to trigger application exit (File > Exit, Save & Exit, etc).
   */
  void render_menu_bar(corundum::tool_host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                       std::vector<TilesetView> &tileset_views, bool &running);

  /** @brief Request the unsaved-changes exit confirmation popup.
   *
   * Use this from input handlers (Q/Esc) that want the same prompt the File > Exit menu item
   * shows when the document is dirty.
   *
   * @param state Editor state — sets state.show_exit_confirm to true.
   */
  void request_exit_confirm(EditorState &state);

  /** @brief Open the "New Tilemap" dialog — used by File > New Map and by main.cpp for the
   * initial launch-with-no-args flow.
   *
   * Safe to call any time; resets the dialog's fields each time. The actual
   * `ImGui::OpenPopup` call is deferred to `render_menu_bar`'s unnested block (ImGui's ID-stack
   * rules require the call site to share the same nesting as the eventual `BeginPopupModal`).
   *
   * @param state Editor state — resets state.new_map_dlg and sets state.new_map_requested.
   */
  void open_new_map_dialog(EditorState &state);

  /** @brief Loads state.map_path into state (map + portals + textures + tileset views), centers
   * the camera, and resets undo to a fresh baseline.
   *
   * Throws std::runtime_error on any load failure. Defined in main.cpp; used there for the
   * CLI-arg startup path and here for File > Open / New Map.
   */
  void finish_map_load(corundum::tool_host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                       std::vector<TilesetView> &tileset_views);

} // namespace tools::tilesmith
