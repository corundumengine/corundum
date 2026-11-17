// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Compile-only check, never linked: built with corundum::rules_interface as its only include root,
// so a rules interface header that includes anything outside that root fails the build. See
// engine/rules_interface/CMakeLists.txt.
#include <corundum/gameplay/player_flags.hpp> // IWYU pragma: keep
#include <corundum/gameplay/rules/rules.hpp>  // IWYU pragma: keep
