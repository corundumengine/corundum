// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/toolkit/widgets/text_buffer.hpp>
#include <doctest/doctest.h>

#include <array>
#include <string>

using corundum::toolkit::widgets::copy_to_buffer;

TEST_CASE("copy_to_buffer copies a short string and null-terminates it") {
  std::array<char, 8> buffer{'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};

  copy_to_buffer(buffer.data(), buffer.size(), "hi");

  CHECK(std::string(buffer.data()) == "hi");
  CHECK(buffer[2] == '\0');
}

TEST_CASE("copy_to_buffer truncates an overlong string to fit the buffer") {
  std::array<char, 4> buffer{};

  copy_to_buffer(buffer.data(), buffer.size(), "abcdef");

  CHECK(std::string(buffer.data()) == "abc");
  CHECK(buffer[3] == '\0');
}

TEST_CASE("copy_to_buffer clears bytes left by a previous longer copy") {
  std::array<char, 8> buffer{};

  copy_to_buffer(buffer.data(), buffer.size(), "longer!");
  copy_to_buffer(buffer.data(), buffer.size(), "x");

  CHECK(std::string(buffer.data()) == "x");
  CHECK(buffer[1] == '\0');
}
