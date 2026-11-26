cmake_minimum_required(VERSION 3.24)
project(RacingGame LANGUAGES C CXX)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw URL https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.4)
FetchContent_Declare(glm URL https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1)
FetchContent_Declare(cgltf URL https://codeload.github.com/jkuhlmann/cgltf/tar.gz/refs/tags/v1.15)
FetchContent_Declare(nlohmann_json URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3)
FetchContent_MakeAvailable(glfw glm cgltf nlohmann_json)

add_executable(RacingGame
    src/main.cpp src/car.cpp src/driving.cpp src/renderer.cpp src/hud.cpp src/tire_effects.cpp src/lighting.cpp)
target_compile_features(RacingGame PRIVATE cxx_std_17)
target_include_directories(RacingGame SYSTEM PRIVATE "${glfw_SOURCE_DIR}/deps" "${cgltf_SOURCE_DIR}")
target_link_libraries(RacingGame PRIVATE glfw glm::glm nlohmann_json::nlohmann_json)
set_target_properties(RacingGame PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

set(car_asset "${CMAKE_CURRENT_SOURCE_DIR}/assets/cars.glb")
file(GLOB gale_asset CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/gale.glb")
set(asset_output "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/assets")
add_custom_target(racing_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${asset_output}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${car_asset}" ${gale_asset} "${asset_output}"
    DEPENDS "${car_asset}" ${gale_asset}
    VERBATIM)
add_dependencies(RacingGame racing_assets)

enable_testing()
add_test(NAME native_smoke COMMAND RacingGame --smoke-test)
if(gale_asset)
    add_test(NAME gale_smoke COMMAND RacingGame --smoke-test --gale)
endif()
