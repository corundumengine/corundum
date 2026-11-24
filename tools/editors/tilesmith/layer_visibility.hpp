// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <vector>

namespace tools::tilesmith {

/** Per-layer editor visibility, parallel to Tilemap::layers. Display state only: never saved, and the
 *  engine ignores it. Hidden layers are not drawn and do not catch clicks; overlays still count them.
 */
using LayerVisibility = std::vector<bool>;

} // namespace tools::tilesmith
