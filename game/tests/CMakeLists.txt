add_executable(gns_tests src/test_main.cpp)
target_link_libraries(gns_tests PRIVATE gns_core)
target_compile_definitions(gns_tests PRIVATE
    GNS_DB_PATH="${CMAKE_SOURCE_DIR}/../gns/gns.db")
add_test(NAME gns_tests COMMAND gns_tests)
