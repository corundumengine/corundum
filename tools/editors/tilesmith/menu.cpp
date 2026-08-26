#include "menu.hpp"
#include "layout.hpp"
#include "new_map_dialog.hpp"
#include "save.hpp"
#include "undo.hpp"
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/tool_host/file_browser.hpp>
#include <format>
#include <imgui.h>
#include <print>

namespace tools::tilemap {

  namespace {
    void do_save(EditorState &state) {
      try_save(state);
    }

    void do_open(corundum::tool_host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                 std::vector<TilesetView> &tileset_views, const std::filesystem::path &path) {
      state.map_path = path;
      try {
        finish_map_load(host, state, texture_store, tileset_views);
        host.set_title("Tilesmith :: " + state.map_path.filename().string());
      } catch (const std::exception &e) {
        state.last_io_error = std::format("Failed to open {}: {}", path.string(), e.what());
        state.show_io_error_popup = true;
        std::println(stderr, "[Tilesmith] {}", state.last_io_error);
      }
    }
  } // namespace

  void request_exit_confirm(EditorState &state) {
    state.show_exit_confirm = true;
  }

  void open_new_map_dialog(EditorState &state) {
    state.new_map_dlg = NewMapDialogState{};
    state.new_map_requested = true;
  }

  void render_menu_bar(corundum::tool_host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                       std::vector<TilesetView> &tileset_views, bool &running) {
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Map..."))
          open_new_map_dialog(state);
        if (ImGui::MenuItem("Open...")) {
          const auto start =
              state.map_path.empty() ? std::filesystem::path("data/tilemaps") : state.map_path.parent_path();
          open_file_browser(state.open_browser, "Open Tilemap", start, {{"Tilemap JSON", {"json"}}});
        }
        if (ImGui::MenuItem("Save", "Cmd+S"))
          do_save(state);
        if (ImGui::MenuItem("Save As...")) {
          const auto start =
              state.map_path.empty() ? std::filesystem::path("data/tilemaps") : state.map_path.parent_path();
          open_save_browser(state.save_browser, "Save Tilemap As", start, {{"Tilemap JSON", {"json"}}},
                            state.map_path.filename().string());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Esc")) {
          if (state.dirty)
            state.show_exit_confirm = true;
          else
            running = false;
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Cmd+Z", false, state.undo.can_undo()))
          apply_undo(state);
        if (ImGui::MenuItem("Redo", "Cmd+Shift+Z", false, state.undo.can_redo()))
          apply_redo(state);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid", "G", &state.show_grid);
        ImGui::MenuItem("Collisions", "C", &state.show_collisions);
        ImGui::MenuItem("Elevation", "E", &state.show_elevation);
        ImGui::MenuItem("Ramps", "R", &state.show_ramps);
        ImGui::MenuItem("Walkability", "W", &state.show_walkability);
        ImGui::MenuItem("Portals", "P", &state.show_portals);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (auto picked = render_file_browser(state.open_browser)) {
      if (state.dirty) {
        state.show_open_confirm = true;
        state.pending_open_path = *picked;
      } else {
        do_open(host, state, texture_store, tileset_views, *picked);
      }
    }
    if (auto picked = render_file_browser(state.save_browser)) {
      state.map_path = *picked;
      do_save(state);
    }

    if (state.show_open_confirm) {
      ImGui::OpenPopup("Unsaved Changes##open");
      state.show_open_confirm = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes##open", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("This map has unsaved changes. Open the new map anyway and discard them?");
      if (ImGui::Button("Discard & Open", ImVec2{160.f, 0.f})) {
        do_open(host, state, texture_store, tileset_views, state.pending_open_path);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{100.f, 0.f}))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    if (state.show_exit_confirm) {
      ImGui::OpenPopup("Unsaved Changes##exit");
      state.show_exit_confirm = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes##exit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("This map has unsaved changes.");
      if (ImGui::Button("Save & Exit", ImVec2{140.f, 0.f})) {
        do_save(state);
        running = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard & Exit", ImVec2{140.f, 0.f})) {
        running = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{100.f, 0.f}))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    if (state.show_io_error_popup) {
      ImGui::OpenPopup("Error");
      state.show_io_error_popup = false;
    }
    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(ImVec4{1.f, .4f, .4f, 1.f}, "%s", state.last_io_error.c_str());
      if (ImGui::Button("OK", ImVec2{100.f, 0.f}))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    if (state.new_map_requested) {
      ImGui::OpenPopup("New Tilemap");
      state.show_new_map_dialog = true;
      state.new_map_requested = false;
    }
    if (state.show_new_map_dialog) {
      render_new_map_dialog(state.new_map_dlg);
      if (state.new_map_dlg.cancelled) {
        state.show_new_map_dialog = false;
      }
      if (state.new_map_dlg.confirmed) {
        auto write_result = write_new_tilemap_json(state.new_map_dlg);
        if (!write_result) {
          state.new_map_dlg.error_msg = write_result.error();
          state.new_map_dlg.confirmed = false;
        } else {
          state.map_path = *write_result;
          try {
            finish_map_load(host, state, texture_store, tileset_views);
            host.set_title("Tilesmith :: " + state.map_path.filename().string());
            state.show_new_map_dialog = false;
          } catch (const std::exception &e) {
            state.last_io_error = std::format("Failed to create new map: {}", e.what());
            state.show_io_error_popup = true;
            state.new_map_dlg.error_msg = e.what();
            state.new_map_dlg.confirmed = false;
            std::println(stderr, "[Tilesmith] {}", state.last_io_error);
          }
        }
      }
    }
  }

} // namespace tools::tilemap
