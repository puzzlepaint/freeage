cmake_minimum_required(VERSION 3.0)

project(FreeAge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)


# Cross-platform threading.
# See: https://cmake.org/cmake/help/latest/module/FindThreads.html
find_package(Threads REQUIRED)


# External dependency: Qt 5 (for GUI and OpenGL function loading)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
# Instruct CMake to run rcc (resource compiler) automatically when needed.
set(CMAKE_AUTORCC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets)


# Packaged dependency: mango (for image loading, especially for the DDS format)
add_subdirectory(third_party/mango/build)


# Packaged dependency: Loguru (for logging)
add_library(loguru SHARED
  third_party/loguru/loguru.cpp
)
set_property(TARGET loguru PROPERTY FOLDER "third_party")
target_include_directories(loguru PUBLIC
  third_party/loguru/
)
set_target_properties(loguru PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_compile_definitions(loguru PRIVATE
  LOGURU_WITH_STREAMS
)
if(MSVC)
  set(LOGURU_DLL_EXPORT "__declspec(dllexport)")
endif()
target_compile_definitions(loguru PRIVATE
  LOGURU_EXPORT=${LOGURU_DLL_EXPORT}
)
target_link_libraries(loguru
  Threads::Threads
  ${CMAKE_DL_LIBS}
)


# Packaged dependency: googletest (for unit tests)
add_subdirectory(third_party/googletest)
set_target_properties(gtest gtest_main PROPERTIES FOLDER "third_party")


# FreeAge base library, used by both the application and the test
# TODO: Unify cpp / cc spelling. Maybe also use hpp to make it clear for headers too that they are C++ code, not C code.
add_library(FreeAgeLib
  src/FreeAge/client_building.cpp
  src/FreeAge/client_unit.cpp
  src/FreeAge/map.cpp
  src/FreeAge/opengl.cc
  src/FreeAge/render_window.cpp
  src/FreeAge/shader_program.cpp
  src/FreeAge/shader_sprite.cpp
  src/FreeAge/sprite.cc
  src/FreeAge/sprite_atlas.cpp
  src/FreeAge/texture.cpp
  
  src/RectangleBinPack/MaxRectsBinPack.cpp
  src/RectangleBinPack/Rect.cpp
)
target_compile_options(FreeAgeLib PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-O2>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-msse2>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-msse3>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-Wno-comment>"
)
target_include_directories(FreeAgeLib PUBLIC
  src
  third_party
  third_party/mango/include
)
target_link_libraries(FreeAgeLib PUBLIC
  Qt5::Widgets
  mango
  loguru
  Threads::Threads
  stdc++fs  # TODO: I think this is specific to gcc version 8
)


# FreeAge application
add_executable(FreeAge
  src/FreeAge/main.cc
)
target_link_libraries(FreeAge
  FreeAgeLib
)


# FreeAge test
add_executable(FreeAgeTest
  src/FreeAge/test.cpp
)
target_link_libraries(FreeAgeTest
  FreeAgeLib
  gtest
)
add_test(FreeAgeTest
  FreeAgeTest
)
