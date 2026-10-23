# Third-party dependencies.
#
# Everything is fetched at configure time via FetchContent (no find_package,
# no manual installs). The only exceptions are OpenGL (GLCore) and the audio
# runtime libraries (threads, libdl, libm), all found and linked in
# engine/src/platform/CMakeLists.txt; doctest is gated by CORUNDUM_BUILD_TESTS.
#
# Each dependency is a release or commit archive checked by URL_HASH, not a git
# clone: FetchContent's shallow clone still fetches every branch tip (196 MB for
# nlohmann/json alone), and the clones cost ~30 s of every fresh configure
# against ~9 s for the archives. To move a pin, change the URL and replace the
# hash with the output of `curl -sL <url> | shasum -a 256`.
#
# FETCHCONTENT_QUIET is forced to FALSE so dependency configure logs stay
# visible.

include_guard(GLOBAL)

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)

# json.tar.xz is the release asset upstream publishes for FetchContent: the CMake
# files and headers without the test suite.
FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    nlohmann_json_schema_validator
    URL      https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.4.1.tar.gz
    URL_HASH SHA256=0c8bf7ba59bd50e312d82da4bab1d49cf3e2211cfbd659472f9c90505d2a4e4c
)
FetchContent_MakeAvailable(nlohmann_json_schema_validator)

if(CORUNDUM_BUILD_TESTS)
  # Test-only dependency: skip the fetch entirely when tests are not built.
  FetchContent_Declare(
        doctest
        URL      https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
        URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    )
  FetchContent_MakeAvailable(doctest)

  # MSVC forward-declares std::basic_ostream, so doctest's generic stringifier
  # cannot stream a std::string_view there; force the real standard headers for
  # every test target (one place, so a new target can't miss it).
  target_compile_definitions(doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
endif()

FetchContent_Declare(
    imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
    URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    glfw
    URL      https://github.com/glfw/glfw/archive/refs/tags/3.5.1.tar.gz
    URL_HASH SHA256=5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    freetype
    URL      https://gitlab.freedesktop.org/freetype/freetype/-/archive/VER-2-15-0/freetype-VER-2-15-0.tar.gz
    URL_HASH SHA256=87c7f1f8699b91e89f773f7dcfa505b044e1dae67f62a55d4e29c3afdcac4fb8
)
FetchContent_MakeAvailable(freetype)

# GLFW and FreeType ship doc comments that clang's -Wdocumentation flags. Mark
# the targets SYSTEM so their include directories are warning-suppressed in
# consumers instead of fixing third-party headers we do not own.
set_target_properties(glfw freetype PROPERTIES SYSTEM TRUE)

# SDL_GameControllerDB: the community gamepad mapping database. GLFW's built-in table is a subset
# that lacks common pads (the 8BitDo Pro 2 on macOS), so the GLFW backend embeds this file's rows
# for the build platform and loads them at startup (engine/src/platform/CMakeLists.txt). Data
# only: the repository has no CMakeLists.txt, so MakeAvailable just populates the source tree.
FetchContent_Declare(
    sdl_gamecontrollerdb
    URL      https://github.com/mdqinc/SDL_GameControllerDB/archive/ff333823a85ae896e73f4c53670932cf0f728adb.tar.gz # master on 20-Oct-2026
    URL_HASH SHA256=dea50426cb94a7b0575f25d861997074834bf98b5a696037f66a1913a2d6b8e1
)
FetchContent_MakeAvailable(sdl_gamecontrollerdb)

FetchContent_Declare(
    sokol
    URL      https://github.com/floooh/sokol/archive/efeb77603e24daea7d1dd6445b9358bb5d82533b.tar.gz # master on 17-Oct-2026
    URL_HASH SHA256=0500b6bf1e10e35a85c28435ec27bb06dbad0948536e5ea37554453842aa0f0f
)
FetchContent_MakeAvailable(sokol)

FetchContent_Declare(
    stb
    URL      https://github.com/nothings/stb/archive/a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d.tar.gz # master on 20-Oct-2026
    URL_HASH SHA256=ae498e10fadeba0c2e7aa86c71cc9989c4d895201cd26b8b72061c76cfde4edf
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    miniaudio
    URL      https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    # Header-only use: a SOURCE_SUBDIR with no CMakeLists.txt makes MakeAvailable only populate the
    # source tree, never configure or build miniaudio's own examples and tests.
    SOURCE_SUBDIR  header-only
)
FetchContent_MakeAvailable(miniaudio)
