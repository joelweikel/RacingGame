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
FetchContent_Declare(stb URL https://codeload.github.com/nothings/stb/tar.gz/6b5799bae8a1fe3792691ef4b72ae26434c3e799
    SOURCE_SUBDIR unused)
FetchContent_Declare(miniaudio URL https://codeload.github.com/mackron/miniaudio/tar.gz/refs/tags/0.11.25
    SOURCE_SUBDIR unused)
if(NOT EMSCRIPTEN)
    FetchContent_MakeAvailable(glfw)
endif()
FetchContent_MakeAvailable(glm cgltf nlohmann_json stb miniaudio)

add_executable(RacingGame
    src/main.cpp src/car.cpp src/driving.cpp src/renderer.cpp src/hud.cpp src/tire_effects.cpp src/lighting.cpp
    src/audio.cpp src/controls.cpp src/menu.cpp src/ui_draw.cpp)
target_compile_features(RacingGame PRIVATE cxx_std_17)
target_include_directories(RacingGame SYSTEM PRIVATE "${cgltf_SOURCE_DIR}" "${stb_SOURCE_DIR}" "${miniaudio_SOURCE_DIR}")
target_link_libraries(RacingGame PRIVATE glm::glm nlohmann_json::nlohmann_json)
set_target_properties(RacingGame PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

set(car_asset "${CMAKE_CURRENT_SOURCE_DIR}/assets/cars.glb")
file(GLOB gale_asset CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/gale.glb")
set(ui_assets
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/Barlow-SemiBold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/BarlowCondensed-ExtraBoldItalic.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/Barlow-OFL.txt")
set(game_assets "${car_asset}" ${gale_asset} ${ui_assets})
if(EMSCRIPTEN)
    set(web_shell "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    set_target_properties(RacingGame PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_compile_options(RacingGame PRIVATE --use-port=contrib.glfw3 -fexceptions)
    target_link_options(RacingGame PRIVATE
        --use-port=contrib.glfw3 -fexceptions
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
        "SHELL:--shell-file \"${web_shell}\"")
    foreach(asset IN LISTS game_assets)
        get_filename_component(asset_name "${asset}" NAME)
        target_link_options(RacingGame PRIVATE "SHELL:--preload-file \"${asset}@/assets/${asset_name}\"")
    endforeach()
    set_property(TARGET RacingGame APPEND PROPERTY LINK_DEPENDS "${web_shell}" ${game_assets})
    return()
endif()

target_include_directories(RacingGame SYSTEM PRIVATE "${glfw_SOURCE_DIR}/deps")
target_link_libraries(RacingGame PRIVATE glfw)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(RacingGame PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
set(asset_output "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/assets")
add_custom_target(racing_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${asset_output}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${game_assets} "${asset_output}"
    DEPENDS ${game_assets}
    VERBATIM)
add_dependencies(RacingGame racing_assets)

enable_testing()
add_test(NAME native_smoke COMMAND RacingGame --smoke-test)
if(gale_asset)
    add_test(NAME gale_smoke COMMAND RacingGame --smoke-test --gale)
endif()
