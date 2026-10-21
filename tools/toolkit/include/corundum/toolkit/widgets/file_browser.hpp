// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <corundum/core/files.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace corundum::toolkit::widgets {

  /// Capacity of FileBrowserState::name_buf, the buffer ImGui's text field writes into.
  inline constexpr std::size_t k_name_buffer_size = 512;

  /// An Open request: list directories plus files whose extension matches one of @p extensions.
  struct OpenRequest {
    /// Extensions to list, compared case-insensitively without a leading dot; empty lists every file.
    std::vector<std::string> extensions;
  };

  /// A Save request: name a file, appending @p extension when the chosen name doesn't carry it.
  struct SaveRequest {
    std::string default_name; ///< Initial contents of the name field.
    std::string extension;    ///< Extension enforced on the chosen name (no leading dot; empty for none).
  };

  /// What a browser is open for; the alternative decides the listing filter and the confirm semantics.
  using FileBrowserRequest = std::variant<OpenRequest, SaveRequest>;

  /**
   * @brief State for one file-browser modal instance.
   *
   * Own one per call site (tilesmith keeps one in EditorState); open it with open_browser() and draw it
   * with render_browser(). Nothing here touches ImGui, so the state transitions are testable without a
   * window.
   *
   * @note The modal's ImGui id carries this state's address, so two browsers never collide; don't move
   * @p fb while its modal is open.
   */
  struct FileBrowserState {
    std::filesystem::path current_dir;               ///< Currently-displayed directory.
    std::vector<corundum::core::DirEntry> entries;   ///< Listing of current_dir, directories first then alphabetical.
    std::array<char, k_name_buffer_size> name_buf{}; ///< Text field holding the chosen filename.
    std::optional<FileBrowserRequest> request;       ///< What the modal is open for; nullopt while closed.
    std::optional<std::size_t> selected_entry;       ///< Index into entries, or nullopt when nothing is highlighted.
    std::string title;                               ///< Modal title, displayed only.
  };

  /**
   * @brief Open @p fb's modal for @p request, titled @p title, starting in @p start_dir.
   *
   * Falls back to the current working directory when @p start_dir isn't a directory. Clears the selection
   * and the name field, then pre-fills the name field from SaveRequest::default_name.
   *
   * @param fb Browser state to populate.
   * @param title Modal title (display only).
   * @param start_dir Directory to begin browsing in.
   * @param request What to browse for.
   */
  void open_browser(FileBrowserState &fb, std::string_view title, const std::filesystem::path &start_dir,
                    FileBrowserRequest request);

  /**
   * @brief Point @p fb at @p dir, reloading its listing.
   *
   * Drops the selection, and in Open mode the name field with it (it mirrors the selection). A directory
   * that can't be listed leaves the listing empty, and @p fb keeps browsing it.
   */
  void navigate_to(FileBrowserState &fb, const std::filesystem::path &dir);

  /**
   * @brief The path confirming @p fb right now would produce.
   *
   * Open: the highlighted entry, when it is a file. Save: the name field relative to current_dir, with
   * SaveRequest::extension appended when the name doesn't already end in it.
   *
   * @return The path, or nullopt when @p fb is closed or nothing valid is chosen.
   */
  [[nodiscard]] std::optional<std::filesystem::path> confirm_path(const FileBrowserState &fb);

  /**
   * @brief Draw the modal if @p fb is open; call every frame until it returns a path.
   *
   * Returns the confirmed path and closes the modal (@c fb.request becomes nullopt) in the frame the user
   * confirms; returns nullopt every other frame, including the frame Cancel closes the modal, so check the
   * return value rather than the state to know whether a path was chosen.
   *
   * @param fb Browser state to draw and update.
   * @return Chosen path, or nullopt if not confirmed this frame.
   */
  [[nodiscard]] std::optional<std::filesystem::path> render_browser(FileBrowserState &fb);

} // namespace corundum::toolkit::widgets
