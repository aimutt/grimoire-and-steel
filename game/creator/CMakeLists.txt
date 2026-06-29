# Shared shop item-art catalog, baked into the Module Creator binary (same images the
# Game Engine renders at play time), referenced by filename id.
gns_generate_item_rc(
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_items.rc"
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_items.h")

add_executable(ModuleCreator src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_items.rc")
target_include_directories(ModuleCreator PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ModuleCreator PRIVATE
    gns_core gns_ui imgui SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image)
if (WIN32)
    target_link_libraries(ModuleCreator PRIVATE comdlg32)   # native file dialogs
endif()
gns_copy_runtime(ModuleCreator)
