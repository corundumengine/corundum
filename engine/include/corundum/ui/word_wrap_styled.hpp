// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/ui/styled_text.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::ui {

  /// One wrapped line: the styled segments that compose it and their total rendered width.
  struct StyledLine {
    std::vector<StyledSegment> segments{};

    float width{0.f};
  };

  namespace detail {

    /// A run of non-space characters. A word may weave through several styles, so it holds one
    /// or more pieces rather than a single style.
    struct StyledWord {
      std::vector<StyledSegment> pieces{};

      bool space_before{false};

      FontStyle space_style{FontStyle::Regular};
    };

    /// A paragraph is the words between two hard newlines. Consecutive newlines yield an empty
    /// paragraph, which wraps to one blank line.
    struct StyledParagraph {
      std::vector<StyledWord> words{};
    };

    /// Splits @p runs into paragraphs of words, collapsing space runs and dropping leading and
    /// trailing spaces. Assumes newlines are already normalized by parse_styled; a '\r' is not
    /// a break here.
    [[nodiscard]] inline std::vector<StyledParagraph> styled_paragraphs(std::span<const StyledRun> runs) {
      std::vector<StyledParagraph> paragraphs;
      StyledParagraph paragraph;
      StyledWord word;
      bool in_word{false};
      bool have_space{false};
      FontStyle space_style{FontStyle::Regular};

      const auto flush_word = [&] {
        if (!in_word)
          return;
        word.space_before = have_space;
        word.space_style = space_style;
        paragraph.words.push_back(std::move(word));
        word = StyledWord{};
        in_word = false;
        have_space = false;
      };
      const auto flush_paragraph = [&] {
        flush_word();
        paragraphs.push_back(std::move(paragraph));
        paragraph = StyledParagraph{};
        have_space = false;
      };

      for (const StyledRun &run : runs) {
        for (const char c : run.text) {
          if (c == '\n') {
            flush_paragraph();
            continue;
          }
          if (c == ' ') {
            flush_word();
            if (!paragraph.words.empty() && !have_space) {
              have_space = true;
              space_style = run.style;
            }
            continue;
          }
          if (!in_word) {
            in_word = true;
            word = StyledWord{};
          }
          if (!word.pieces.empty() && word.pieces.back().style == run.style) {
            word.pieces.back().text.push_back(c);
          } else {
            StyledSegment piece;
            piece.text.push_back(c);
            piece.style = run.style;
            word.pieces.push_back(std::move(piece));
          }
        }
      }
      flush_paragraph();
      return paragraphs;
    }

    /// Appends @p text with @p style to @p line, merging into the last segment when the style
    /// matches, and advances the line width by the measured text.
    template <class MeasureFn>
    void append_styled(StyledLine &line, std::string_view text, FontStyle style, const MeasureFn &measure) {
      if (!line.segments.empty() && line.segments.back().style == style) {
        line.segments.back().text.append(text);
      } else {
        StyledSegment segment;
        segment.text.assign(text);
        segment.style = style;
        segment.x = line.width;
        line.segments.push_back(std::move(segment));
      }
      line.width += measure(text, style);
    }

  } // namespace detail

  /// Splits styled text into lines no wider than @p max_width as reported by @p measure, with the
  /// same greedy behavior as ui::wrap_text: hard newlines force a break, space runs collapse, and
  /// a word wider than @p max_width is placed on its own line rather than broken. A word keeps
  /// its own style, and a separating space takes the style of the run it sat in, so a caller can
  /// draw each segment at @c line.segments[i].x without reconstructing styles.
  /// @param runs Styled runs, typically from parse_styled, which normalizes CRLF first.
  /// @param measure Callable (std::string_view, FontStyle) -> float returning rendered width.
  /// @return At least one line; empty input yields a single empty line.
  /// @pre @p max_width <= 0 places every word on its own line.
  template <class MeasureFn>
  [[nodiscard]] std::vector<StyledLine> wrap_styled(std::span<const StyledRun> runs, float max_width,
                                                    MeasureFn measure) {
    const std::vector<detail::StyledParagraph> paragraphs = detail::styled_paragraphs(runs);
    std::vector<StyledLine> lines;
    lines.reserve(paragraphs.size());

    for (const detail::StyledParagraph &paragraph : paragraphs) {
      StyledLine line;
      bool has_content{false};
      for (const detail::StyledWord &word : paragraph.words) {
        float word_width{0.f};
        for (const StyledSegment &piece : word.pieces)
          word_width += measure(piece.text, piece.style);

        float space_width{0.f};
        if (has_content && word.space_before)
          space_width = measure(" ", word.space_style);

        if (has_content && line.width + space_width + word_width > max_width) {
          lines.push_back(std::move(line));
          line = StyledLine{};
          has_content = false;
          space_width = 0.f;
        }

        if (has_content && word.space_before)
          detail::append_styled(line, " ", word.space_style, measure);
        for (const StyledSegment &piece : word.pieces)
          detail::append_styled(line, piece.text, piece.style, measure);
        has_content = true;
      }
      lines.push_back(std::move(line));
    }
    return lines;
  }

} // namespace corundum::ui
