#include "new_map_dialog.hpp"
#include <algorithm>
#include <corundum/core/json_io.hpp>
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace tools::tilemap {

  namespace {

    [[nodiscard]] static bool is_valid_id(std::string_view s) noexcept {
      if (s.empty())
        return false;
      return std::ranges::all_of(s, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      });
    }

    // Returns a non-empty error string if validation fails, empty string on success.
    [[nodiscard]] static std::string validate(const NewMapDialogState &dlg) {
      if (!is_valid_id(dlg.id))
        return "Map ID must be non-empty and contain only letters, digits, _ or -.";
      if (dlg.width <= 0 || dlg.height <= 0)
        return "Width and Height must both be greater than zero.";
      if (dlg.iso_diamond_w <= 0 || dlg.iso_diamond_h <= 0)
        return "Tile footprint width and height must both be greater than zero.";
      if (dlg.layer_name[0] == '\0')
        return "Layer name must not be empty.";
      if (dlg.tileset_source[0] == '\0')
        return "Tileset source path must not be empty.";
      if (!std::filesystem::exists(dlg.tileset_source))
        return std::string{"Tileset not found: "} + dlg.tileset_source;
      const auto dest = std::filesystem::path{"data/tilemaps"} / (std::string{dlg.id} + ".json");
      if (std::filesystem::exists(dest))
        return std::string{"A tilemap already exists at: "} + dest.string();
      return {};
    }

  } // namespace

  void render_new_map_dialog(NewMapDialogState &dlg) {
    // Open the modal on the very first call.
    static bool opened = false;
    if (!opened) {
      ImGui::OpenPopup("New Tilemap");
      opened = true;
    }

    // Center the modal in the window.
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});

    if (!ImGui::BeginPopupModal("New Tilemap", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      return;

    ImGui::TextUnformatted("Map ID (used as filename, e.g. \"dungeon_01\"):");
    ImGui::SetNextItemWidth(320.f);
    ImGui::InputText("##id", dlg.id, sizeof(dlg.id));

    ImGui::Spacing();
    ImGui::TextUnformatted("Dimensions (tiles):");
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("Width##w", &dlg.width);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("Height##h", &dlg.height);

    ImGui::Spacing();
    ImGui::TextUnformatted("Tile footprint (pixels) — independent from sprite image size:");
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("Diamond W##dw", &dlg.iso_diamond_w);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("Diamond H##dh", &dlg.iso_diamond_h);

    ImGui::Spacing();
    ImGui::TextUnformatted("Initial layer name:");
    ImGui::SetNextItemWidth(320.f);
    ImGui::InputText("##layer", dlg.layer_name, sizeof(dlg.layer_name));

    ImGui::Spacing();
    ImGui::TextUnformatted("Tileset source (relative to project root):");
    ImGui::TextDisabled("  e.g. data/sprite_sheets/objects/terrain.json");
    ImGui::SetNextItemWidth(480.f);
    ImGui::InputText("##source", dlg.tileset_source, sizeof(dlg.tileset_source));

    ImGui::Spacing();

    if (!dlg.error_msg.empty()) {
      ImGui::TextColored({1.f, 0.35f, 0.35f, 1.f}, "%s", dlg.error_msg.c_str());
      ImGui::Spacing();
    }

    if (ImGui::Button("Create", {120.f, 0.f})) {
      const std::string err = validate(dlg);
      if (!err.empty()) {
        dlg.error_msg = err;
      } else {
        dlg.error_msg.clear();
        dlg.confirmed = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit", {120.f, 0.f})) {
      dlg.cancelled = true;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  std::expected<std::filesystem::path, std::string> write_new_tilemap_json(const NewMapDialogState &dlg) {
    const std::filesystem::path dest = std::filesystem::path{"data/tilemaps"} / (std::string{dlg.id} + ".json");

    nlohmann::json j;
    j["id"] = dlg.id;
    j["width"] = dlg.width;
    j["height"] = dlg.height;
    j["iso_diamond_w"] = dlg.iso_diamond_w;
    j["iso_diamond_h"] = dlg.iso_diamond_h;
    j["tilesets"] = nlohmann::json::array({{{"first_gid", 0}, {"source", dlg.tileset_source}}});
    j["layers"] = nlohmann::json::array({{{"name", dlg.layer_name}, {"objects", nlohmann::json::array()}}});

    {
      auto res = corundum::core::write_json(dest, j);
      if (!res)
        return std::unexpected(res.error());
    }

    return dest;
  }

} // namespace tools::tilemap
