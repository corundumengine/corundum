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

    /// Accumulates paragraphs of styled words one character at a time. A one-shot builder behind
    /// styled_paragraphs(): feed it every character, then finish().
    class StyledParagraphBuilder {
    public:
      void consume(char c, FontStyle style) {
        if (c == '\n') {
          flush_paragraph();
        } else if (c == ' ') {
          absorb_space(style);
        } else {
          append_to_word(c, style);
        }
      }

      [[nodiscard]] std::vector<StyledParagraph> finish() {
        flush_paragraph();
        return std::move(paragraphs_);
      }

    private:
      void flush_word() {
        if (!in_word_)
          return;
        word_.space_before = have_space_;
        word_.space_style = space_style_;
        paragraph_.words.push_back(std::move(word_));
        word_ = StyledWord{};
        in_word_ = false;
        have_space_ = false;
      }

      void flush_paragraph() {
        flush_word();
        paragraphs_.push_back(std::move(paragraph_));
        paragraph_ = StyledParagraph{};
        have_space_ = false;
      }

      void absorb_space(FontStyle style) {
        flush_word();
        if (!paragraph_.words.empty() && !have_space_) {
          have_space_ = true;
          space_style_ = style;
        }
      }

      void append_to_word(char c, FontStyle style) {
        if (!in_word_) {
          in_word_ = true;
          word_ = StyledWord{};
        }
        if (!word_.pieces.empty() && word_.pieces.back().style == style) {
          word_.pieces.back().text.push_back(c);
        } else {
          StyledSegment piece;
          piece.text.push_back(c);
          piece.style = style;
          word_.pieces.push_back(std::move(piece));
        }
      }

      std::vector<StyledParagraph> paragraphs_;
      StyledParagraph paragraph_;
      StyledWord word_;
      bool in_word_{false};
      bool have_space_{false};
      FontStyle space_style_{FontStyle::Regular};
    };

    /// Splits @p runs into paragraphs of words, collapsing space runs and dropping leading and
    /// trailing spaces. Assumes newlines are already normalized by parse_styled; a '\r' is not
    /// a break here.
    [[nodiscard]] inline std::vector<StyledParagraph> styled_paragraphs(std::span<const StyledRun> runs) {
      StyledParagraphBuilder builder;
      for (const StyledRun &run : runs)
        for (const char c : run.text)
          builder.consume(c, run.style);
      return builder.finish();
    }

    /// Appends @p text with @p style to @p line, merging into the last segment when the style
    /// matches, and advances the line width by @p width.
    inline void append_styled(StyledLine &line, std::string_view text, FontStyle style, float width) {
      if (!line.segments.empty() && line.segments.back().style == style) {
        line.segments.back().text.append(text);
      } else {
        StyledSegment segment;
        segment.text.assign(text);
        segment.style = style;
        segment.x = line.width;
        line.segments.push_back(std::move(segment));
      }
      line.width += width;
    }

    /// Measures each piece of @p word into @p piece_widths (cleared first) and returns the word's
    /// total width. The caller owns the buffer so it is reused across words without allocating.
    template <class MeasureFn>
    [[nodiscard]] float measure_word(const StyledWord &word, const MeasureFn &measure,
                                     std::vector<float> &piece_widths) {
      piece_widths.clear();
      float word_width{0.f};
      for (const StyledSegment &piece : word.pieces) {
        const float piece_width = measure(piece.text, piece.style);
        piece_widths.push_back(piece_width);
        word_width += piece_width;
      }
      return word_width;
    }

    /// Appends @p word using the widths measure_word() already computed, so no piece is measured
    /// twice.
    inline void append_measured_word(StyledLine &line, const StyledWord &word, std::span<const float> piece_widths) {
      for (std::size_t i = 0; i < word.pieces.size(); ++i)
        append_styled(line, word.pieces[i].text, word.pieces[i].style, piece_widths[i]);
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
    std::vector<float> piece_widths;

    for (const detail::StyledParagraph &paragraph : paragraphs) {
      StyledLine line;
      bool has_content{false};
      for (const detail::StyledWord &word : paragraph.words) {
        const float word_width = detail::measure_word(word, measure, piece_widths);
        const float space_width = (has_content && word.space_before) ? measure(" ", word.space_style) : 0.f;

        if (has_content && line.width + space_width + word_width > max_width) {
          lines.push_back(std::move(line));
          line = StyledLine{};
          has_content = false;
        }

        if (has_content && word.space_before)
          detail::append_styled(line, " ", word.space_style, space_width);
        detail::append_measured_word(line, word, piece_widths);
        has_content = true;
      }
      lines.push_back(std::move(line));
    }
    return lines;
  }

} // namespace corundum::ui
