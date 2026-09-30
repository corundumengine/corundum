// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/utf8.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace core = corundum::core;

TEST_CASE("decode_utf8: ASCII passes through one byte at a time") {
  const std::string_view text = "A";
  std::size_t i = 0;
  CHECK(core::decode_utf8(text, i) == static_cast<uint32_t>('A'));
  CHECK(i == 1);
}

TEST_CASE("decode_utf8: valid 2-, 3-, and 4-byte sequences decode and advance by their length") {
  {
    const std::string_view text = "\xC3\xA9"; // é, U+00E9
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == 0xE9u);
    CHECK(i == 2);
  }
  {
    const std::string_view text = "\xE2\x80\x94"; // em dash, U+2014 (the real-world case)
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == 0x2014u);
    CHECK(i == 3);
  }
  {
    const std::string_view text = "\xF0\x9F\x98\x80"; // grinning face, U+1F600
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == 0x1F600u);
    CHECK(i == 4);
  }
}

TEST_CASE("decode_utf8: a truncated sequence returns the replacement codepoint and advances by one") {
  const std::string_view text = "\xE2\x80"; // 3-byte lead, missing third byte
  std::size_t i = 0;
  CHECK(core::decode_utf8(text, i) == core::k_utf8_replacement_char);
  CHECK(i == 1);
}

TEST_CASE("decode_utf8: stray continuation and invalid lead bytes return the replacement codepoint") {
  {
    const std::string_view text = "\x80";
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == core::k_utf8_replacement_char);
    CHECK(i == 1);
  }
  {
    const std::string_view text = "\xFF";
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == core::k_utf8_replacement_char);
    CHECK(i == 1);
  }
}

TEST_CASE("decode_utf8: overlong, surrogate, and out-of-range encodings are rejected") {
  {
    const std::string_view text = "\xC0\xAF"; // overlong '/'
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == core::k_utf8_replacement_char);
    CHECK(i == 1);
  }
  {
    const std::string_view text = "\xED\xA0\x80"; // UTF-16 surrogate half U+D800
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == core::k_utf8_replacement_char);
    CHECK(i == 1);
  }
  {
    const std::string_view text = "\xF4\x90\x80\x80"; // U+110000, above the UTF-8 maximum
    std::size_t i = 0;
    CHECK(core::decode_utf8(text, i) == core::k_utf8_replacement_char);
    CHECK(i == 1);
  }
}

TEST_CASE("decode_utf8: a mixed string iterates to completion") {
  const std::string_view text = "A\xC3\xA9\xE2\x80\x94"
                                "B";
  const std::array<uint32_t, 4> expected = {'A', 0xE9u, 0x2014u, 'B'};

  std::size_t i = 0;
  std::size_t count = 0;
  while (i < text.size()) {
    CHECK(core::decode_utf8(text, i) == expected[count]);
    ++count;
  }
  CHECK(count == 4);
  CHECK(i == text.size());
}
