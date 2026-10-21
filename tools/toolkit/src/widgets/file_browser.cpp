// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/files.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <corundum/toolkit/widgets/text_buffer.hpp>
#include <cstddef>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace corundum::toolkit::widgets {

  namespace {
    /// ImGui id that stays unique per browser instance; the part after "###" is hashed but not displayed.
    [[nodiscard]] std::string popup_id_of(const FileBrowserState &fb) {
      return std::format("{}###{}", fb.title, static_cast<const void *>(&fb));
    }

    /// Draw the listing; @p double_clicked receives the file double-clicked this frame, if any.
    void render_entries(FileBrowserState &fb, std::optional<std::filesystem::path> &double_clicked) {
      ImGui::BeginChild("##entries", ImVec2{0.f, -ImGui::GetFrameHeightWithSpacing() * 2.f}, ImGuiChildFlags_Borders);
      for (std::size_t index = 0; index < fb.entries.size(); ++index) {
        const corundum::core::DirEntry &entry = fb.entries[index];
        const std::string label = entry.is_dir ? ("[dir] " + entry.name) : entry.name;
        if (!ImGui::Selectable(label.c_str(), fb.selected_entry == index, ImGuiSelectableFlags_AllowDoubleClick))
          continue;
        fb.selected_entry = index;
        if (!entry.is_dir)
          copy_to_buffer(fb.name_buf.data(), fb.name_buf.size(), entry.name);
        if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          continue;
        if (entry.is_dir) {
          // navigate_to() replaces the listing, so leave the loop before touching fb.entries again.
          const std::filesystem::path dir = entry.path;
          navigate_to(fb, dir);
          break;
        }
        double_clicked = entry.path;
      }
      ImGui::EndChild();
    }

    /// Draw the confirm/cancel row: confirm writes the chosen path to @p result, cancel closes the modal.
    void render_confirm_row(FileBrowserState &fb, const char *label, std::optional<std::filesystem::path> &result) {
      const std::optional<std::filesystem::path> choice = confirm_path(fb);
      ImGui::BeginDisabled(!choice.has_value());
      if (ImGui::Button(label, ImVec2{100.f, 0.f}))
        result = choice;
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{100.f, 0.f})) {
        fb.request.reset();
        ImGui::CloseCurrentPopup();
      }
    }
  } // namespace

  std::optional<std::filesystem::path> render_browser(FileBrowserState &fb) {
    if (!fb.request)
      return std::nullopt;

    const std::string popup_id = popup_id_of(fb);
    ImGui::OpenPopup(popup_id.c_str());
    ImGui::SetNextWindowSize(ImVec2{520.f, 420.f}, ImGuiCond_FirstUseEver);

    std::optional<std::filesystem::path> result;
    if (!ImGui::BeginPopupModal(popup_id.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings))
      return result;

    if (ImGui::Button("Up") && fb.current_dir.has_parent_path())
      navigate_to(fb, fb.current_dir.parent_path());
    ImGui::SameLine();
    ImGui::TextUnformatted(fb.current_dir.string().c_str());
    ImGui::Separator();

    std::optional<std::filesystem::path> double_clicked;
    render_entries(fb, double_clicked);

    const bool saving = std::holds_alternative<SaveRequest>(*fb.request);
    if (saving) {
      ImGui::SetNextItemWidth(-80.f);
      ImGui::InputText("##filename", fb.name_buf.data(), fb.name_buf.size());
    } else {
      result = std::move(double_clicked);
    }

    if (!result)
      render_confirm_row(fb, saving ? "Save" : "Open", result);

    if (result) {
      fb.request.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
  }

} // namespace corundum::toolkit::widgets
