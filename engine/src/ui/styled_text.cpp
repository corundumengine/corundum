// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/styled_text.hpp>

#include <corundum/core/utf8.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/word_wrap.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::ui {

  namespace {

    /// One token of the raw stream: either literal text or a run of 1-3 asterisks that may
    /// become a delimiter. `stars == 0` marks literal text.
    struct Token {
      std::string text{};

      int stars{0};
    };

    /// Splits @p text into literal-text and asterisk-delimiter tokens, resolving `\*` and `\\`.
    /// Asterisk runs longer than three are literal text.
    std::vector<Token> tokenize(std::string_view text) {
      std::vector<Token> tokens;
      std::string buffer;

      std::size_t i{};
      while (i < text.size()) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '*' || text[i + 1] == '\\')) {
          buffer.push_back(text[i + 1]);
          i += 2;
          continue;
        }
        if (c != '*') {
          buffer.push_back(c);
          ++i;
          continue;
        }

        std::size_t run{};
        while (i + run < text.size() && text[i + run] == '*')
          ++run;
        if (run > 3) {
          buffer.append(run, '*');
          i += run;
          continue;
        }

        if (!buffer.empty()) {
          tokens.push_back(Token{.text = std::move(buffer)});
          buffer.clear();
        }
        Token delimiter;
        delimiter.text.assign(run, '*');
        delimiter.stars = static_cast<int>(run);
        tokens.push_back(std::move(delimiter));
        i += run;
      }

      if (!buffer.empty())
        tokens.push_back(Token{.text = std::move(buffer)});
      return tokens;
    }

    /// Maps the bold/italic bits to the FontStyle the run should render with.
    FontStyle style_of(bool bold, bool italic) {
      if (bold)
        return italic ? FontStyle::BoldItalic : FontStyle::Bold;
      return italic ? FontStyle::Italic : FontStyle::Regular;
    }

    /// Appends @p text with @p style, merging into the previous run when the style matches.
    void append_run(std::vector<StyledRun> &runs, std::string_view text, FontStyle style) {
      if (!runs.empty() && runs.back().style == style) {
        runs.back().text.append(text);
        return;
      }
      StyledRun run;
      run.text.assign(text);
      run.style = style;
      runs.push_back(std::move(run));
    }

  } // namespace

  std::vector<StyledRun> parse_styled(std::string_view text) {
    std::string normalized;
    const bool has_carriage_return{text.contains('\r')};
    if (has_carriage_return)
      normalized = detail::normalize_newlines(text);
    const std::string_view source{has_carriage_return ? std::string_view{normalized} : text};

    const std::vector<Token> tokens = tokenize(source);

    // Pair each delimiter with the nearest earlier unmatched delimiter of the same length.
    std::vector<bool> matched(tokens.size(), false);
    std::vector<std::size_t> stack;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
      if (tokens[i].stars == 0)
        continue;
      if (!stack.empty() && tokens[stack.back()].stars == tokens[i].stars) {
        matched[stack.back()] = true;
        matched[i] = true;
        stack.pop_back();
        continue;
      }
      stack.push_back(i);
    }

    std::vector<StyledRun> runs;
    bool bold{false};
    bool italic{false};
    for (std::size_t i = 0; i < tokens.size(); ++i) {
      const Token &token = tokens[i];
      if (token.stars == 0 || !matched[i]) {
        append_run(runs, token.text, style_of(bold, italic));
        continue;
      }
      switch (token.stars) {
        case 1:
          italic = !italic;
          break;
        case 2:
          bold = !bold;
          break;
        default:
          bold = !bold;
          italic = !italic;
          break;
      }
    }
    return runs;
  }

  std::pair<std::vector<StyledSegment>, int> reveal_prefix_styled(std::span<const StyledSegment> segments, int budget) {
    std::vector<StyledSegment> revealed;
    if (budget <= 0)
      return {revealed, 0};

    int consumed{};
    for (const StyledSegment &segment : segments) {
      if (segment.text.empty())
        continue;

      std::size_t offset{};
      while (offset < segment.text.size() && consumed < budget) {
        (void)core::decode_utf8(segment.text, offset);
        ++consumed;
      }

      StyledSegment truncated = segment;
      if (offset < segment.text.size()) {
        truncated.text = segment.text.substr(0, offset);
        revealed.push_back(std::move(truncated));
        return {revealed, consumed};
      }
      revealed.push_back(std::move(truncated));
      if (consumed >= budget)
        return {revealed, consumed};
    }
    return {revealed, consumed};
  }

} // namespace corundum::ui
