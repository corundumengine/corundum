// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/ui/font_family.hpp>
#include <corundum/ui/styled_text.hpp>

#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

using corundum::ui::FontStyle;
using corundum::ui::parse_styled;
using corundum::ui::reveal_prefix_styled;
using corundum::ui::StyledRun;
using corundum::ui::StyledSegment;

namespace {

  StyledRun run(std::string text, FontStyle style) {
    StyledRun result;
    result.text = std::move(text);
    result.style = style;
    return result;
  }

  StyledSegment segment(std::string text, FontStyle style, float x) {
    StyledSegment result;
    result.text = std::move(text);
    result.style = style;
    result.x = x;
    return result;
  }

  std::vector<StyledRun> runs(std::initializer_list<std::pair<const char *, FontStyle>> entries) {
    std::vector<StyledRun> result;
    for (const auto &[text, style] : entries)
      result.push_back(run(text, style));
    return result;
  }

  std::vector<StyledSegment> make_segments(std::initializer_list<StyledSegment> entries) {
    return {entries.begin(), entries.end()};
  }

} // namespace

TEST_CASE("parse_styled — italic") {
  CHECK(parse_styled("*hi*") == runs({{"hi", FontStyle::Italic}}));
}

TEST_CASE("parse_styled — bold") {
  CHECK(parse_styled("**hi**") == runs({{"hi", FontStyle::Bold}}));
}

TEST_CASE("parse_styled — bold italic") {
  CHECK(parse_styled("***hi***") == runs({{"hi", FontStyle::BoldItalic}}));
}

TEST_CASE("parse_styled — text around a styled span") {
  CHECK(parse_styled("a *b* c") ==
        runs({{"a ", FontStyle::Regular}, {"b", FontStyle::Italic}, {" c", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — sequential spans do not leak style") {
  CHECK(parse_styled("*a* *b*") ==
        runs({{"a", FontStyle::Italic}, {" ", FontStyle::Regular}, {"b", FontStyle::Italic}}));
}

TEST_CASE("parse_styled — escaped asterisk is literal") {
  CHECK(parse_styled("a\\*b") == runs({{"a*b", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — escaped backslash is literal") {
  CHECK(parse_styled("a\\\\b") == runs({{"a\\b", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — backslash before other characters stays literal") {
  CHECK(parse_styled("a\\nb") == runs({{"a\\nb", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — unterminated italic delimiter is literal") {
  CHECK(parse_styled("*abc") == runs({{"*abc", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — unterminated bold delimiter is literal") {
  CHECK(parse_styled("**abc") == runs({{"**abc", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — unmatched trailing delimiter merges with preceding text") {
  CHECK(parse_styled("a*b*c*") ==
        runs({{"a", FontStyle::Regular}, {"b", FontStyle::Italic}, {"c*", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — run longer than three asterisks is literal") {
  CHECK(parse_styled("****a****") == runs({{"****a****", FontStyle::Regular}}));
}

TEST_CASE("parse_styled — UTF-8 passes through a styled span") {
  CHECK(parse_styled("*caf\xC3\xA9*") == runs({{"caf\xC3\xA9", FontStyle::Italic}}));
}

TEST_CASE("parse_styled — empty input yields no runs") {
  CHECK(parse_styled("").empty());
}

TEST_CASE("parse_styled — CRLF normalizes to a single newline") {
  CHECK(parse_styled("a\r\nb") == runs({{"a\nb", FontStyle::Regular}}));
}

TEST_CASE("reveal_prefix_styled — budget covers every segment") {
  const auto source = make_segments({
      segment("ab", FontStyle::Regular, 0.f),
      segment("cd", FontStyle::Bold, 5.f),
      segment("ef", FontStyle::Italic, 10.f),
  });
  const auto [revealed, consumed] = reveal_prefix_styled(source, 6);
  CHECK(consumed == 6);
  CHECK(revealed == source);
}

TEST_CASE("reveal_prefix_styled — budget splits a segment mid-run") {
  const auto source = make_segments({
      segment("ab", FontStyle::Regular, 0.f),
      segment("cd", FontStyle::Bold, 5.f),
      segment("ef", FontStyle::Italic, 10.f),
  });
  const auto [revealed, consumed] = reveal_prefix_styled(source, 3);
  CHECK(consumed == 3);
  CHECK(revealed == make_segments({segment("ab", FontStyle::Regular, 0.f), segment("c", FontStyle::Bold, 5.f)}));
}

TEST_CASE("reveal_prefix_styled — zero budget reveals nothing") {
  const auto source = make_segments({segment("ab", FontStyle::Regular, 0.f)});
  const auto [revealed, consumed] = reveal_prefix_styled(source, 0);
  CHECK(consumed == 0);
  CHECK(revealed.empty());
}

TEST_CASE("reveal_prefix_styled — negative budget reveals nothing") {
  const auto source = make_segments({segment("ab", FontStyle::Regular, 0.f)});
  const auto [revealed, consumed] = reveal_prefix_styled(source, -4);
  CHECK(consumed == 0);
  CHECK(revealed.empty());
}

TEST_CASE("reveal_prefix_styled — budget beyond the total reveals everything") {
  const auto source = make_segments({segment("ab", FontStyle::Regular, 0.f), segment("cd", FontStyle::Bold, 5.f)});
  const auto [revealed, consumed] = reveal_prefix_styled(source, 100);
  CHECK(consumed == 4);
  CHECK(revealed == source);
}

TEST_CASE("reveal_prefix_styled — counts codepoints, not bytes") {
  const auto source = make_segments({segment("caf\xC3\xA9", FontStyle::Regular, 0.f)});
  const auto [revealed, consumed] = reveal_prefix_styled(source, 3);
  CHECK(consumed == 3);
  REQUIRE(revealed.size() == 1);
  CHECK(revealed[0].text == "caf");
}

TEST_CASE("reveal_prefix_styled — empty segments are skipped") {
  const auto source = make_segments(
      {segment("", FontStyle::Regular, 0.f), segment("ab", FontStyle::Bold, 0.f), segment("", FontStyle::Italic, 2.f)});
  const auto [revealed, consumed] = reveal_prefix_styled(source, 1);
  CHECK(consumed == 1);
  CHECK(revealed == make_segments({segment("a", FontStyle::Bold, 0.f)}));
}
