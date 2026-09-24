// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdint>
#include <string_view>

#include "encoding.hpp"

using corundum::world::tilemap::flip_flags_from_string;
using corundum::world::tilemap::k_flip_h;
using corundum::world::tilemap::k_flip_v;
using corundum::world::tilemap::ramp_axis_from_string;
using corundum::world::tilemap::RampAxis;
using corundum::world::tilemap::to_string;
using corundum::world::tilemap::triangle_cut_from_string;
using corundum::world::tilemap::TriangleCut;

TEST_CASE("triangle cut spellings round-trip through their JSON form") {
  CHECK(to_string(TriangleCut::NorthWest) == "NW");
  CHECK(to_string(TriangleCut::NorthEast) == "NE");
  CHECK(to_string(TriangleCut::SouthWest) == "SW");
  CHECK(to_string(TriangleCut::SouthEast) == "SE");

  CHECK(triangle_cut_from_string("NW") == TriangleCut::NorthWest);
  CHECK(triangle_cut_from_string("NE") == TriangleCut::NorthEast);
  CHECK(triangle_cut_from_string("SW") == TriangleCut::SouthWest);
  CHECK(triangle_cut_from_string("SE") == TriangleCut::SouthEast);
}

TEST_CASE("unrecognized triangle cut spellings are rejected") {
  CHECK_FALSE(triangle_cut_from_string("nw").has_value());
  CHECK_FALSE(triangle_cut_from_string("").has_value());
  CHECK_FALSE(triangle_cut_from_string("N").has_value());
}

TEST_CASE("ramp axis spellings round-trip through their JSON form") {
  CHECK(to_string(RampAxis::NorthSouth) == "ns");
  CHECK(to_string(RampAxis::EastWest) == "ew");

  CHECK(ramp_axis_from_string("ns") == RampAxis::NorthSouth);
  CHECK(ramp_axis_from_string("ew") == RampAxis::EastWest);
}

TEST_CASE("unrecognized ramp axis spellings are rejected") {
  CHECK_FALSE(ramp_axis_from_string("NS").has_value());
  CHECK_FALSE(ramp_axis_from_string("").has_value());
}

TEST_CASE("flip flag spellings round-trip through their JSON form") {
  CHECK(to_string(k_flip_h) == "H");
  CHECK(to_string(k_flip_v) == "V");
  CHECK(to_string(static_cast<uint8_t>(k_flip_h | k_flip_v)) == "HV");

  CHECK(to_string(static_cast<uint8_t>(0)) == "");

  CHECK(flip_flags_from_string("H") == k_flip_h);
  CHECK(flip_flags_from_string("V") == k_flip_v);
  CHECK(flip_flags_from_string("HV") == (k_flip_h | k_flip_v));
}

TEST_CASE("unrecognized flip flag spellings are rejected") {
  CHECK_FALSE(flip_flags_from_string("h").has_value());
  CHECK_FALSE(flip_flags_from_string("").has_value());
  CHECK_FALSE(flip_flags_from_string("VH").has_value());
}
