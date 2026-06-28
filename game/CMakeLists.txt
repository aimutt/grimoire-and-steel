cmake_minimum_required(VERSION 3.21)
project(GrimoireAndSteel LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

# --------------------------------------------------------------------------
# Vendored prebuilt SDL2 / SDL2_image (VC dev packages in third_party/)
# --------------------------------------------------------------------------
set(SDL2_DIR       "${CMAKE_SOURCE_DIR}/third_party/SDL2/cmake")
set(SDL2_image_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2_image/cmake")
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

# --------------------------------------------------------------------------
# sqlite3 amalgamation (vendored)
# --------------------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_FTS5)

# --------------------------------------------------------------------------
# Dear ImGui (vendored) + SDL2 backends
# --------------------------------------------------------------------------
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
    third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# --------------------------------------------------------------------------
# Helper: copy SDL DLLs + gns.db next to an app executable after build
# --------------------------------------------------------------------------
function(gns_copy_runtime tgt)
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/SDL2/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:${tgt}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/SDL2_image/lib/x64/SDL2_image.dll" "$<TARGET_FILE_DIR:${tgt}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/../gns/gns.db" "$<TARGET_FILE_DIR:${tgt}>/data/gns.db"
        VERBATIM)
endfunction()

add_subdirectory(core)
add_subdirectory(shared)
add_subdirectory(engine)
add_subdirectory(creator)

enable_testing()
add_subdirectory(tests)
