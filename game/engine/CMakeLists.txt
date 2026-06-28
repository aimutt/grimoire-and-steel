add_executable(GameEngine src/main.cpp)
target_link_libraries(GameEngine PRIVATE
    gns_core gns_ui imgui SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image)
gns_copy_runtime(GameEngine)
