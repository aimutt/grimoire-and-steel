# CMake generated Testfile for 
# Source directory: C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests
# Build directory: C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[gns_tests]=] "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/build/tests/Debug/gns_tests.exe")
  set_tests_properties([=[gns_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;5;add_test;C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[gns_tests]=] "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/build/tests/Release/gns_tests.exe")
  set_tests_properties([=[gns_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;5;add_test;C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[gns_tests]=] "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/build/tests/MinSizeRel/gns_tests.exe")
  set_tests_properties([=[gns_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;5;add_test;C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[gns_tests]=] "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/build/tests/RelWithDebInfo/gns_tests.exe")
  set_tests_properties([=[gns_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;5;add_test;C:/Users/rockr/source/repos-aimutt/grimoire-and-steel/game/tests/CMakeLists.txt;0;")
else()
  add_test([=[gns_tests]=] NOT_AVAILABLE)
endif()
