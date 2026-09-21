// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/ui/word_wrap.hpp>
#include <string_view>

// measure = character count (proxy for fixed-width font)
namespace {
  float char_count(std::string_view s) {
    return static_cast<float>(s.size());
  }
} // namespace

TEST_CASE("wrap_text — basic overflow") {
  auto lines = corundum::ui::wrap_text("hello world foo bar", 12.f, char_count);
  // "hello world" = 11 chars ✓, "foo bar" = 7 chars ✓
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "hello world");
  CHECK(lines[1] == "foo bar");
}

TEST_CASE("wrap_text — hard newline") {
  auto lines = corundum::ui::wrap_text("line one\nline two", 100.f, char_count);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "line one");
  CHECK(lines[1] == "line two");
}

TEST_CASE("wrap_text — empty input yields one empty line") {
  auto lines = corundum::ui::wrap_text("", 10.f, char_count);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].empty());
}

TEST_CASE("wrap_text — oversized word gets its own line") {
  auto lines = corundum::ui::wrap_text("abcdefghij klm", 4.f, char_count);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "abcdefghij");
  CHECK(lines[1] == "klm");
}

TEST_CASE("wrap_text — max_width <= 0 still terminates") {
  const auto zero = corundum::ui::wrap_text("a b c", 0.f, char_count);
  REQUIRE(zero.size() == 3);
  CHECK(zero[0] == "a");
  CHECK(zero[1] == "b");
  CHECK(zero[2] == "c");

  const auto negative = corundum::ui::wrap_text("a b", -5.f, char_count);
  REQUIRE(negative.size() == 2);
  CHECK(negative[0] == "a");
  CHECK(negative[1] == "b");
}

TEST_CASE("wrap_text — blank lines between paragraphs are preserved") {
  auto lines = corundum::ui::wrap_text("a\n\nb", 10.f, char_count);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0] == "a");
  CHECK(lines[1].empty());
  CHECK(lines[2] == "b");
}

TEST_CASE("wrap_text — trailing newline appends a blank line") {
  auto lines = corundum::ui::wrap_text("abc\n", 10.f, char_count);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "abc");
  CHECK(lines[1].empty());
}

TEST_CASE("wrap_text — spaces collapse and leading spaces drop") {
  auto lines = corundum::ui::wrap_text("   a  b", 100.f, char_count);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "a b");
}

TEST_CASE("wrap_text — CRLF is normalized to a single break") {
  auto lines = corundum::ui::wrap_text("abc\r\ndef", 100.f, char_count);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "abc");
  CHECK(lines[1] == "def");
}

TEST_CASE("wrap_text — lone CR is a line break") {
  auto lines = corundum::ui::wrap_text("abc\rdef", 100.f, char_count);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "abc");
  CHECK(lines[1] == "def");
}
