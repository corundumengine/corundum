#include <doctest/doctest.h>

#include <corundum/platform/resource_ids.hpp>

#include <cstdint>
#include <type_traits>

/** A texture id is its own type: a raw integer or a font id cannot pass for one. */
static_assert(!std::is_convertible_v<std::uint32_t, corundum::platform::TextureId>);
static_assert(!std::is_convertible_v<corundum::platform::TextureId, std::uint32_t>);
static_assert(!std::is_convertible_v<std::uint32_t, corundum::platform::FontId>);
static_assert(!std::is_convertible_v<corundum::platform::FontId, corundum::platform::TextureId>);
static_assert(!std::is_convertible_v<corundum::platform::TextureId, corundum::platform::FontId>);
