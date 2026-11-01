# The rules interface: the headers a game's ruleset compiles against, and nothing else. They have
# their own include root, so a ruleset that links only this target (cerclet) can include exactly
# these headers; any other <corundum/...> header fails to compile there. corundum_gameplay links
# it PUBLIC, so the framework and the game see the same headers at the same paths.
include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

add_library(corundum_rules_interface INTERFACE)
add_library(corundum::rules_interface ALIAS corundum_rules_interface)
target_include_directories(corundum_rules_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# corundum_use_cxx23() sets the feature PUBLIC, which an INTERFACE library can't take.
target_compile_features(corundum_rules_interface INTERFACE cxx_std_23)

# Compiles the headers with nothing but this target on the include path, so a header that includes
# anything outside its root fails every build that adds corundum, a ruleset's standalone one too.
add_library(corundum_rules_interface_self_contained OBJECT self_contained.cpp)
target_link_libraries(corundum_rules_interface_self_contained PRIVATE corundum::rules_interface)
corundum_enable_warnings(corundum_rules_interface_self_contained)
