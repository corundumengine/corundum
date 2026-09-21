// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace corundum::core {

  /// Codepoint returned for a truncated or malformed UTF-8 sequence.
  inline constexpr uint32_t k_utf8_replacement_char = 0xFFFD;
  /// Largest codepoint UTF-8 can encode; anything above it is malformed.
  inline constexpr uint32_t k_utf8_max_codepoint = 0x10FFFF;

  /// Decodes one UTF-8 codepoint starting at `text[i]` and advances `i` past
  /// it (by 1-4 bytes on success, by exactly 1 byte on a malformed sequence,
  /// so callers always make forward progress).
  ///
  /// Rejects overlong encodings, UTF-16 surrogate halves (U+D800-U+DFFF), and
  /// codepoints above U+10FFFF, returning k_utf8_replacement_char for each.
  /// @pre i < text.size()
  [[nodiscard]] inline uint32_t decode_utf8(std::string_view text, std::size_t &i) {
    const auto b0 = static_cast<unsigned char>(text[i]);
    if (b0 < 0x80) {
      ++i;
      return b0;
    }

    int extra = 0;
    uint32_t codepoint = 0;
    uint32_t min_codepoint = 0;     // overlong-encoding floor for this sequence length
    if (b0 >= 0xC2 && b0 <= 0xDF) { // 0xC0/0xC1 would encode an overlong 2-byte form
      extra = 1;
      codepoint = b0 & 0x1Fu;
      min_codepoint = 0x80;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
      extra = 2;
      codepoint = b0 & 0x0Fu;
      min_codepoint = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) { // 0xF5-0xF7 would exceed U+10FFFF
      extra = 3;
      codepoint = b0 & 0x07u;
      min_codepoint = 0x10000;
    } else {
      ++i; // stray continuation byte or invalid lead byte
      return k_utf8_replacement_char;
    }

    if (i + static_cast<std::size_t>(extra) >= text.size()) {
      ++i; // truncated sequence at end of string
      return k_utf8_replacement_char;
    }

    for (int k = 1; k <= extra; ++k) {
      const auto b = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
      if ((b & 0xC0u) != 0x80u) {
        ++i; // not a continuation byte — malformed
        return k_utf8_replacement_char;
      }
      codepoint = (codepoint << 6u) | (b & 0x3Fu);
    }

    if (codepoint < min_codepoint || codepoint > k_utf8_max_codepoint || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
      ++i; // overlong, out of range, or a surrogate half
      return k_utf8_replacement_char;
    }

    i += static_cast<std::size_t>(extra) + 1;
    return codepoint;
  }

} // namespace corundum::core
