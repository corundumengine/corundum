#include <doctest/doctest.h>

#include <corundum/gameplay/word_wrap.hpp>

TEST_CASE("wrap_text — basic overflow") {
  // measure = character count (proxy for fixed-width font)
  auto measure = [](std::string_view s) { return static_cast<float>(s.size()); };

  auto lines = corundum::gameplay::wrap_text("hello world foo bar", 12.f, measure);
  // "hello world" = 11 chars ✓, "foo bar" = 7 chars ✓
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "hello world");
  CHECK(lines[1] == "foo bar");
}

TEST_CASE("wrap_text — hard newline") {
  auto measure = [](std::string_view s) { return static_cast<float>(s.size()); };
  auto lines = corundum::gameplay::wrap_text("line one\nline two", 100.f, measure);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "line one");
  CHECK(lines[1] == "line two");
}
