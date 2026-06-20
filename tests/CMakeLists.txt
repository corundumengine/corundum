cmake_minimum_required(VERSION 3.28)
project(corundum_tests LANGUAGES CXX)

enable_testing()

# The test executable
add_executable(corundum_tests
    main.cpp
    test_actions.cpp
    test_collision.cpp
    test_dialogue.cpp
    test_game_config.cpp
    test_handle_pool.cpp
    test_word_wrap.cpp
    test_tilemap_loader.cpp
    test_world_manifest.cpp
    test_iso_math.cpp
)

# Link to engine and doctest
target_link_libraries(corundum_tests
    PRIVATE
        engine
        doctest::doctest
)

target_compile_options(corundum_tests PRIVATE -Wall -Wextra -Wpedantic)

# Enable CTest integration
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(corundum_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
