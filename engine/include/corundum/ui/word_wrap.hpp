// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::ui {

  namespace detail {
    /// Rewrites "\r\n" and lone '\r' to '\n' so wrapping only has to split on '\n'.
    /// @return A copy of @p text with every carriage return normalized.
    [[nodiscard]] inline std::string normalize_newlines(std::string_view text) {
      std::string normalized;
      normalized.reserve(text.size());
      for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\r') {
          normalized.push_back(text[i]);
          continue;
        }
        normalized.push_back('\n');
        if (i + 1 < text.size() && text[i + 1] == '\n')
          ++i; // consume the '\n' of a "\r\n" pair
      }
      return normalized;
    }

    /// Greedy word-wrap of a single segment that contains no hard newline characters.
    /// Splits on single spaces; runs of spaces collapse and leading/trailing spaces are
    /// dropped. A word that alone exceeds @p max_width is placed on its own line rather
    /// than broken, so the caller must tolerate an over-wide line.
    /// @param measure Callable (std::string_view) -> float returning rendered width.
    /// @return At least one line; an all-space segment yields a single empty line.
    template <typename MeasureFn>
    [[nodiscard]] std::vector<std::string> wrap_segment(std::string_view seg, float max_width, MeasureFn measure) {
      std::vector<std::string> lines;
      std::string line;
      std::size_t word_start = 0;
      while (word_start <= seg.size()) {
        const std::size_t space = seg.find(' ', word_start);
        const std::size_t end = (space == std::string_view::npos) ? seg.size() : space;
        const std::string_view word = seg.substr(word_start, end - word_start);

        if (!word.empty()) {
          std::string candidate = line.empty() ? std::string(word) : line + ' ' + std::string(word);

          if (measure(std::string_view{candidate}) <= max_width) {
            line = std::move(candidate);
          } else {
            if (!line.empty())
              lines.push_back(std::move(line));
            line = std::string(word); // force oversized word onto its own line
          }
        }
        word_start = end + 1;
      }
      lines.push_back(std::move(line)); // flush; empty string == blank line
      return lines;
    }
  } // namespace detail

  /// Splits `text` into lines no wider than `max_width` as reported by `measure`.
  /// Hard newlines force a line break; "\r\n" and lone '\r' are normalized to '\n'
  /// first, so CRLF input never leaks a carriage return into a line. Spaces within a
  /// line collapse (see detail::wrap_segment), and a single word wider than `max_width`
  /// is placed on its own line rather than looping forever.
  /// @param measure Callable (std::string_view) -> float returning rendered width.
  /// @return At least one line; empty input yields a single empty line.
  /// @pre @p max_width <= 0 places every word on its own line (callers pass a
  ///      viewport-derived width, so this only guards degenerate layout).
  /// @pre @p measure is non-decreasing as characters are appended, so an over-wide
  ///      line never becomes narrow enough for a later word to join it.
  template <typename MeasureFn>
  [[nodiscard]] std::vector<std::string> wrap_text(std::string_view text, float max_width, MeasureFn measure) {
    std::vector<std::string> result;

    std::string normalized;
    const bool has_carriage_return = text.contains('\r');
    if (has_carriage_return)
      normalized = detail::normalize_newlines(text);
    const std::string_view wrapped = has_carriage_return ? std::string_view{normalized} : text;

    std::size_t seg_start = 0;
    while (true) {
      // Split on hard newlines first
      const std::size_t nl = wrapped.find('\n', seg_start);
      const std::size_t seg_end = (nl == std::string_view::npos) ? wrapped.size() : nl;
      const auto seg = wrapped.substr(seg_start, seg_end - seg_start);

      auto lines = detail::wrap_segment(seg, max_width, measure);
      result.insert(result.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));

      if (nl == std::string_view::npos)
        break;
      seg_start = nl + 1;
    }
    return result;
  }

} // namespace corundum::ui
