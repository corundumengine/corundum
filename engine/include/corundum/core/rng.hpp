// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/verify.hpp>
#include <cstdint>

namespace corundum::core {

  /** @brief Deterministic PCG32 (pcg_setseq_64_xsh_rr_32) generator. Same seed and stream → same sequence
   *  on every platform. Not thread-safe; one instance per consumer. */
  class Rng {
  public:
    /** @brief Full generator state, for save/load and replay. */
    struct State {
      std::uint64_t state{};
      std::uint64_t inc{};
      bool operator==(const State &) const = default;
    };

    /// Seeds as pcg32_srandom_r(seed, stream).
    explicit Rng(std::uint64_t seed = 0x853c49e6748fea9bULL, std::uint64_t stream = 0xda3e39cb94b95bdbULL) noexcept;

    [[nodiscard]] std::uint32_t next_u32() noexcept;

    /// Uniform integer in [lo, hi] inclusive, unbiased (Lemire's method). @pre lo <= hi.
    [[nodiscard]] std::int32_t range(std::int32_t lo, std::int32_t hi) noexcept;

    /// Uniform float in [0, 1), built from the top 24 bits of next_u32().
    [[nodiscard]] float unit() noexcept;

    /// True with probability @p p, clamped to [0, 1]; chance(0) is always false and chance(1) always true.
    [[nodiscard]] bool chance(float p) noexcept;

    [[nodiscard]] State state() const noexcept;

    void set_state(State s) noexcept;

  private:
    /// Uniform draw in [0, bound), unbiased. @pre bound > 0.
    [[nodiscard]] std::uint32_t uniform_bounded(std::uint32_t bound) noexcept;

    State s_{};
  };

  inline Rng::Rng(std::uint64_t seed, std::uint64_t stream) noexcept {
    s_.state = 0U;
    s_.inc = (stream << 1U) | 1U;
    static_cast<void>(next_u32());
    s_.state += seed;
    static_cast<void>(next_u32());
  }

  inline std::uint32_t Rng::next_u32() noexcept {
    const std::uint64_t old_state = s_.state;
    s_.state = (old_state * 6364136223846793005ULL) + s_.inc;
    const auto xorshifted = static_cast<std::uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(old_state >> 59U);
    return (xorshifted >> rotation) | (xorshifted << ((0U - rotation) & 31U));
  }

  inline std::uint32_t Rng::uniform_bounded(std::uint32_t bound) noexcept {
    std::uint64_t product = static_cast<std::uint64_t>(next_u32()) * bound;
    auto low = static_cast<std::uint32_t>(product);
    if (low < bound) {
      // 2^32 % bound = (2^32 - bound) % bound, evaluated without overflow.
      const std::uint32_t threshold = (0U - bound) % bound;
      while (low < threshold) {
        product = static_cast<std::uint64_t>(next_u32()) * bound;
        low = static_cast<std::uint32_t>(product);
      }
    }
    return static_cast<std::uint32_t>(product >> 32U);
  }

  inline std::int32_t Rng::range(std::int32_t lo, std::int32_t hi) noexcept {
    core::verify(lo <= hi, "Rng::range: lo must not exceed hi");
    const std::uint32_t span =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo)) + 1U;
    // span == 0 only for the full 32-bit range, where a raw draw is already uniform.
    const std::uint32_t offset = span == 0U ? next_u32() : uniform_bounded(span);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(lo) + offset);
  }

  inline float Rng::unit() noexcept {
    return static_cast<float>(next_u32() >> 8U) * 0x1p-24f;
  }

  inline bool Rng::chance(float p) noexcept {
    if (!(p > 0.f))
      return false; // also catches NaN
    if (p >= 1.f)
      return true;
    return unit() < p;
  }

  inline Rng::State Rng::state() const noexcept {
    return s_;
  }

  inline void Rng::set_state(State s) noexcept {
    s_ = s;
  }

} // namespace corundum::core
