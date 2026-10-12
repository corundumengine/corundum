// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// miniaudio implementation translation unit. miniaudio enables its Vorbis decoder only when
// stb_vorbis has been declared first (it checks STB_VORBIS_INCLUDE_STB_VORBIS_H), and stb_vorbis's
// own implementation must come after miniaudio's — the include order below is load-bearing.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)
#ifdef __clang__
#pragma clang diagnostic pop
#endif
