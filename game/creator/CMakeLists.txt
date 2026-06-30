# Shared shop item-art catalog, baked into the Module Creator binary (same images the
# Game Engine renders at play time), referenced by filename id.
gns_generate_item_rc(
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_items.rc"
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_items.h")

# Bake the shared Grimoire & Steel splash (the same PNG the Game Engine shows on startup)
# into the binary as a GNSSPLASH RCDATA resource, loaded via gns::ui::loadEmbeddedTexture.
set(_splash "${CMAKE_SOURCE_DIR}/engine/assets/gns-splash.png")
string(REPLACE "/" "\\\\" _splashrc "${_splash}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/embedded_splash.rc" "GNSSPLASH RCDATA \"${_splashrc}\"\n")

add_executable(ModuleCreator src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_items.rc"
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_splash.rc")
target_include_directories(ModuleCreator PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ModuleCreator PRIVATE
    gns_core gns_ui imgui SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image)
if (WIN32)
    target_link_libraries(ModuleCreator PRIVATE comdlg32)   # native file dialogs
endif()
gns_copy_runtime(ModuleCreator)
