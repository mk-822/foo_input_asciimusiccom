cmake_minimum_required(VERSION 3.20)
project(foo_input_asciimusiccom VERSION 0.1.17 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(musiccom_core STATIC
    src/mml.cpp src/player.cpp
    third_party/ymfm/src/ymfm_adpcm.cpp
    third_party/ymfm/src/ymfm_opn.cpp
    third_party/ymfm/src/ymfm_pcm.cpp
    third_party/ymfm/src/ymfm_ssg.cpp)
target_include_directories(musiccom_core PUBLIC include third_party/ymfm/src)

add_executable(musiccom_render tools/render.cpp)
target_link_libraries(musiccom_render PRIVATE musiccom_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(musiccom_tests tests/test_mml.cpp)
  target_link_libraries(musiccom_tests PRIVATE musiccom_core)
  add_test(NAME musiccom_tests COMMAND musiccom_tests)
endif()

# The foobar2000 SDK consists of several MSBuild projects with configuration
# mappings of their own. Build foo_input_asciimusiccom.vcxproj (or run
# tools/build.ps1)
# for the distributable component; CMake intentionally covers the portable
# parser/player, renderer and tests only.
