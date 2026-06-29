# Shared UI helpers (Dear ImGui map rendering) used by both GameEngine and ModuleCreator.
# Kept out of gns_core so the domain library stays UI-free.
add_library(gns_ui STATIC src/MapRender.cpp)
target_include_directories(gns_ui PUBLIC include)
target_link_libraries(gns_ui PUBLIC imgui gns_core SDL2::SDL2 SDL2_image::SDL2_image)
