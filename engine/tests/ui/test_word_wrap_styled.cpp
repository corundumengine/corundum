// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/ui/font_family.hpp>
#include <corundum/ui/styled_text.hpp>
#include <corundum/ui/word_wrap_styled.hpp>

#include <span>
#include <string_view>
#include <vector>

using corundum::ui::FontStyle;
using corundum::ui::parse_styled;
using corundum::ui::StyledLine;
using corundum::ui::StyledRun;
using corundum::ui::wrap_styled;

namespace {

  // Character count proxy for a fixed-width font.
  float char_count(std::string_view s, FontStyle /*style*/) {
    return static_cast<float>(s.size());
  }

  // Bold text is far wider, so a line that fits a bold word must have measured it as bold.
  float bold_is_wide(std::string_view s, FontStyle style) {
    return static_cast<float>(s.size()) + (style == FontStyle::Bold ? 100.f : 0.f);
  }

  StyledRun run(const char *text, FontStyle style) {
    StyledRun result;
    result.text = text;
    result.style = style;
    return result;
  }

  std::vector<StyledLine> wrap_parsed(std::string_view text, float max_width) {
    const auto runs = parse_styled(text);
    return wrap_styled(std::span<const StyledRun>{runs}, max_width, char_count);
  }

} // namespace

TEST_CASE("wrap_styled — wraps across a style boundary") {
  const auto lines = wrap_parsed("hello **world** foo", 12.f);
  REQUIRE(lines.size() == 2);

  REQUIRE(lines[0].segments.size() == 2);
  CHECK(lines[0].segments[0].text == "hello ");
  CHECK(lines[0].segments[0].style == FontStyle::Regular);
  CHECK(lines[0].segments[0].x == 0.f);
  CHECK(lines[0].segments[1].text == "world");
  CHECK(lines[0].segments[1].style == FontStyle::Bold);
  CHECK(lines[0].segments[1].x == 6.f);
  CHECK(lines[0].width == 11.f);

  REQUIRE(lines[1].segments.size() == 1);
  CHECK(lines[1].segments[0].text == "foo");
  CHECK(lines[1].segments[0].style == FontStyle::Regular);
  CHECK(lines[1].segments[0].x == 0.f);
}

TEST_CASE("wrap_styled — a bold word is measured with the bold measure") {
  const auto runs = parse_styled("aaaa **bb**");
  const auto lines = wrap_styled(std::span<const StyledRun>{runs}, 20.f, bold_is_wide);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].segments[0].text == "aaaa");
  CHECK(lines[1].segments[0].text == "bb");
  CHECK(lines[1].segments[0].style == FontStyle::Bold);
}

TEST_CASE("wrap_styled — hard newline breaks the line") {
  const auto lines = wrap_parsed("line one\nline two", 100.f);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].segments[0].text == "line one");
  CHECK(lines[1].segments[0].text == "line two");
}

TEST_CASE("wrap_styled — CRLF normalizes to a single break") {
  const auto lines = wrap_parsed("abc\r\ndef", 100.f);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].segments[0].text == "abc");
  CHECK(lines[1].segments[0].text == "def");
}

TEST_CASE("wrap_styled — spaces collapse") {
  const auto lines = wrap_parsed("   a   b   ", 100.f);
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].segments.size() == 1);
  CHECK(lines[0].segments[0].text == "a b");
}

TEST_CASE("wrap_styled — a separating space takes the style of its run") {
  const std::vector<StyledRun> runs{
      run("a", FontStyle::Regular),
      run(" ", FontStyle::Bold),
      run("b", FontStyle::Regular),
  };
  const auto lines = wrap_styled(std::span<const StyledRun>{runs}, 100.f, char_count);
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].segments.size() == 3);
  CHECK(lines[0].segments[0].text == "a");
  CHECK(lines[0].segments[0].x == 0.f);
  CHECK(lines[0].segments[1].text == " ");
  CHECK(lines[0].segments[1].style == FontStyle::Bold);
  CHECK(lines[0].segments[1].x == 1.f);
  CHECK(lines[0].segments[2].text == "b");
  CHECK(lines[0].segments[2].x == 2.f);
  CHECK(lines[0].width == 3.f);
}

TEST_CASE("wrap_styled — a single word wider than the line gets its own line") {
  const auto lines = wrap_parsed("abcdef klm", 4.f);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].segments[0].text == "abcdef");
  CHECK(lines[1].segments[0].text == "klm");
}

TEST_CASE("wrap_styled — max_width <= 0 still terminates") {
  const auto lines = wrap_parsed("a b c", 0.f);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].segments[0].text == "a");
  CHECK(lines[1].segments[0].text == "b");
  CHECK(lines[2].segments[0].text == "c");
}

TEST_CASE("wrap_styled — blank lines between paragraphs are preserved") {
  const auto lines = wrap_parsed("a\n\nb", 100.f);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].segments[0].text == "a");
  CHECK(lines[1].segments.empty());
  CHECK(lines[2].segments[0].text == "b");
}

TEST_CASE("wrap_styled — trailing newline appends a blank line") {
  const auto lines = wrap_parsed("abc\n", 100.f);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].segments[0].text == "abc");
  CHECK(lines[1].segments.empty());
}

TEST_CASE("wrap_styled — empty input yields one empty line") {
  const auto lines = wrap_parsed("", 100.f);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].segments.empty());
  CHECK(lines[0].width == 0.f);
}

TEST_CASE("wrap_styled — style is preserved across a wrapped continuation") {
  const auto lines = wrap_parsed("**aaaa bbbb cccc**", 9.f);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].segments[0].style == FontStyle::Bold);
  CHECK(lines[0].segments[0].text == "aaaa bbbb");
  CHECK(lines[1].segments[0].style == FontStyle::Bold);
  CHECK(lines[1].segments[0].text == "cccc");
}
