#pragma once
#include <cstdint>
#include <string_view>

namespace corundum::core::string {

  /// @brief Compile-time or runtime hashed string ID.
  ///
  /// Uses FNV-1a 32-bit hash. Two StringIds are equal iff their source strings
  /// hashed to the same value — hash collisions are not detected at runtime.
  /// Use the `_sid` UDL for compile-time constants.
  ///
  /// @code
  ///   using namespace corundum::core::string::literals;
  ///   constexpr StringId k_player = "player"_sid;
  ///   if (tag == k_player) { ... }
  /// @endcode
  struct StringId {
    uint32_t value{0};

    constexpr bool operator==(const StringId &) const noexcept = default;
    constexpr bool operator!=(const StringId &) const noexcept = default;

    constexpr explicit operator bool() const noexcept {
      return value != 0;
    }
  };

  namespace detail {

    [[nodiscard]] consteval uint32_t fnv1a(std::string_view s) noexcept {
      uint32_t h = 2166136261u;
      for (const char c : s) {
        h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        h *= 16777619u;
      }
      return h;
    }

  } // namespace detail

  /// @brief Hash a string at runtime.
  [[nodiscard]] constexpr StringId make_sid(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (const char c : s) {
      h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
      h *= 16777619u;
    }
    return StringId{h};
  }

  namespace literals {

    /// @brief Compile-time StringId literal: `"player"_sid`.
    [[nodiscard]] consteval StringId operator""_sid(const char *s, std::size_t n) noexcept {
      return StringId{detail::fnv1a(std::string_view{s, n})};
    }

  } // namespace literals

} // namespace corundum::core::string
