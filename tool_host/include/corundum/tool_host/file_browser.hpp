#pragma once
#include <corundum/core/files.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::tool_host {

  /**
   * @brief One selectable extension filter, e.g. {"Tilemap JSON", {"json"}}.
   *
   * Extensions are compared case-insensitively, without a leading dot.
   */
  struct FileBrowserFilter {
    std::string label;                   ///< Human-readable filter name.
    std::vector<std::string> extensions; ///< Extensions matched by this filter.
  };

  /// Browser operation mode.
  enum class FileBrowserMode { Open, Save };

  /**
   * @brief State for one file-browser modal instance.
   *
   * Own one per call site (tilesmith keeps one in EditorState); call open()/render() — nothing
   * here touches ImGui until render() runs.
   */
  struct FileBrowserState {
    bool active = false;                          ///< True while the modal is on screen.
    FileBrowserMode mode = FileBrowserMode::Open; ///< Open vs Save semantics.
    std::string title;                            ///< Modal title (also used as the popup's ID).
    std::filesystem::path current_dir;            ///< Currently-displayed directory.
    std::vector<FileBrowserFilter> filters;       ///< Active extension filters (Open mode only).

    /// Directory listing (corundum::core::DirEntry), refreshed whenever current_dir changes.
    std::vector<corundum::core::DirEntry> entries;
    int selected_entry = -1; ///< Index of the highlighted entry, or -1.

    char name_buf[512] = {}; ///< Editable filename (Save mode) / path field (both modes).
  };

  /**
   * @brief Open the browser in Open mode, starting at @p start_dir.
   *
   * Falls back to the current working directory if @p start_dir doesn't exist.
   *
   * @param fb Browser state to populate.
   * @param title Modal title (also used as the popup's ID).
   * @param start_dir Directory to begin browsing in.
   * @param filters Optional extension filters; empty means "show everything".
   */
  void open_file_browser(FileBrowserState &fb, std::string_view title, const std::filesystem::path &start_dir,
                         std::vector<FileBrowserFilter> filters = {});

  /**
   * @brief Open the browser in Save mode, pre-filling the filename field with @p default_name.
   *
   * Falls back to the current working directory if @p start_dir doesn't exist.
   *
   * @param fb Browser state to populate.
   * @param title Modal title (also used as the popup's ID).
   * @param start_dir Directory to begin browsing in.
   * @param filters Optional extension filters; empty means "show everything".
   * @param default_name Initial filename to pre-fill in the input field.
   */
  void open_save_browser(FileBrowserState &fb, std::string_view title, const std::filesystem::path &start_dir,
                         std::vector<FileBrowserFilter> filters, std::string_view default_name);

  /**
   * @brief Render the modal if @p fb is active.
   *
   * Returns the chosen path once the user confirms (Open: an existing file double-clicked or
   * "Open" pressed with a selection; Save: "Save" pressed with a non-empty filename field) —
   * sets @c fb.active = false in that case. Returns nullopt every other frame (still open, or
   * just cancelled — @c fb.active is false after Cancel too, so check the return value, not
   * @c fb.active, to know whether a path was chosen this frame).
   *
   * @param fb Browser state to render.
   * @return Chosen path, or nullopt if not confirmed this frame.
   */
  [[nodiscard]] std::optional<std::filesystem::path> render_file_browser(FileBrowserState &fb);

} // namespace corundum::tool_host
