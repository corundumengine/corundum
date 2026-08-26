#include <algorithm>
#include <corundum/core/files.hpp>
#include <corundum/tool_host/file_browser.hpp>
#include <imgui.h>

namespace corundum::tool_host {

  namespace {
    void copy_to_buf(char *buf, std::size_t buf_size, std::string_view s) {
      const std::size_t n = std::min(s.size(), buf_size - 1);
      std::copy_n(s.data(), n, buf);
      buf[n] = '\0';
    }

    bool matches_filters(const std::filesystem::path &p, const std::vector<FileBrowserFilter> &filters) {
      if (filters.empty())
        return true;
      for (const auto &f : filters)
        for (const auto &ext : f.extensions)
          if (corundum::core::has_extension(p, ext))
            return true;
      return false;
    }

    void refresh_entries(FileBrowserState &fb) {
      fb.entries.clear();
      fb.selected_entry = -1;
      for (auto &e : corundum::core::list_dir_entries(fb.current_dir)) {
        if (!e.is_dir && fb.mode == FileBrowserMode::Open && !matches_filters(e.path, fb.filters))
          continue;
        fb.entries.push_back(std::move(e));
      }
      // list_dir_entries() already sorts directories-first/alphabetical; the filter pass above
      // preserves that order (only removes non-matching files), so no re-sort needed here.
    }

    void navigate(FileBrowserState &fb, const std::filesystem::path &dir) {
      fb.current_dir = dir;
      refresh_entries(fb);
    }
  } // namespace

  void open_file_browser(FileBrowserState &fb, std::string_view title, const std::filesystem::path &start_dir,
                         std::vector<FileBrowserFilter> filters) {
    fb.active = true;
    fb.mode = FileBrowserMode::Open;
    fb.title = title;
    fb.filters = std::move(filters);
    fb.name_buf[0] = '\0';
    navigate(fb, std::filesystem::is_directory(start_dir) ? start_dir : std::filesystem::current_path());
  }

  void open_save_browser(FileBrowserState &fb, std::string_view title, const std::filesystem::path &start_dir,
                         std::vector<FileBrowserFilter> filters, std::string_view default_name) {
    fb.active = true;
    fb.mode = FileBrowserMode::Save;
    fb.title = title;
    fb.filters = std::move(filters);
    copy_to_buf(fb.name_buf, sizeof(fb.name_buf), default_name);
    navigate(fb, std::filesystem::is_directory(start_dir) ? start_dir : std::filesystem::current_path());
  }

  std::optional<std::filesystem::path> render_file_browser(FileBrowserState &fb) {
    if (!fb.active)
      return std::nullopt;

    ImGui::OpenPopup(fb.title.c_str());
    ImGui::SetNextWindowSize(ImVec2{520.f, 420.f}, ImGuiCond_FirstUseEver);
    std::optional<std::filesystem::path> result;

    if (ImGui::BeginPopupModal(fb.title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
      if (ImGui::Button("Up") && fb.current_dir.has_parent_path())
        navigate(fb, fb.current_dir.parent_path());
      ImGui::SameLine();
      ImGui::TextUnformatted(fb.current_dir.string().c_str());

      ImGui::Separator();
      ImGui::BeginChild("##entries", ImVec2{0.f, -ImGui::GetFrameHeightWithSpacing() * 2.f}, ImGuiChildFlags_Borders);
      for (std::size_t i = 0; i < fb.entries.size(); ++i) {
        const auto &e = fb.entries[i];
        const std::string label = e.is_dir ? ("[dir] " + e.name) : e.name;
        if (ImGui::Selectable(label.c_str(), fb.selected_entry == static_cast<int>(i),
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          fb.selected_entry = static_cast<int>(i);
          if (!e.is_dir)
            copy_to_buf(fb.name_buf, sizeof(fb.name_buf), e.name);
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (e.is_dir)
              navigate(fb, e.path);
            else if (fb.mode == FileBrowserMode::Open)
              result = e.path;
          }
        }
      }
      ImGui::EndChild();

      if (fb.mode == FileBrowserMode::Save) {
        ImGui::SetNextItemWidth(-80.f);
        ImGui::InputText("##filename", fb.name_buf, sizeof(fb.name_buf));
      }

      const bool have_name = fb.name_buf[0] != '\0';
      const char *confirm_label = fb.mode == FileBrowserMode::Open ? "Open" : "Save";
      if (!result) {
        ImGui::BeginDisabled(!have_name);
        if (ImGui::Button(confirm_label, ImVec2{100.f, 0.f}))
          result = fb.current_dir / fb.name_buf;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{100.f, 0.f})) {
          fb.active = false;
          ImGui::CloseCurrentPopup();
        }
      }

      if (result) {
        fb.active = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    return result;
  }

} // namespace corundum::tool_host
