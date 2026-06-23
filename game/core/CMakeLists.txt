add_library(gns_core STATIC
    src/Database.cpp
    src/Content.cpp
    src/Dice.cpp
    src/Repository.cpp
    src/Character.cpp
    src/Rules.cpp
    src/ModuleIO.cpp
    src/Session.cpp
    src/PlotTracker.cpp)
target_include_directories(gns_core PUBLIC include)
target_link_libraries(gns_core PUBLIC sqlite3)
