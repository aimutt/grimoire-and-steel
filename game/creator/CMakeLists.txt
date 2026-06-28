add_executable(ModuleCreator src/main.cpp)
target_link_libraries(ModuleCreator PRIVATE
    gns_core gns_ui imgui SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image)
if (WIN32)
    target_link_libraries(ModuleCreator PRIVATE comdlg32)   # native file dialogs
endif()
gns_copy_runtime(ModuleCreator)
