// One-time sokol_imgui implementation translation unit.
// SOKOL_METAL / SOKOL_GLCORE / SOKOL_D3D11 / SOKOL_IMGUI_NO_SOKOL_APP
// are injected by CMake compile definitions.
// No other translation unit in this link unit must define SOKOL_IMGUI_IMPL.
#include <imgui.h>     // Must precede sokol_imgui.h
#include <sokol_gfx.h> // Must precede sokol_imgui.h
#define SOKOL_IMGUI_IMPL
#include <sokol_imgui.h>
