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
find_package(Qt5 REQUIRED COMPONENTS Widgets Network)


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
  src/FreeAge/common/messages.cpp
  src/FreeAge/common/timing.cpp
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
  Qt5::Network
  mango
  loguru
  Threads::Threads
  stdc++fs  # TODO: I think this is specific to gcc version 8
)


# FreeAge server application
add_executable(FreeAgeServer
  src/FreeAge/server/main.cpp
)
target_link_libraries(FreeAgeServer
  FreeAgeLib
)


# FreeAge application
set(FREEAGE_SRCS
  resources/resources.qrc
  src/FreeAge/client/building.cpp
  src/FreeAge/client/game_dialog.cpp
  src/FreeAge/client/health_bar.cpp
  src/FreeAge/client/main.cpp
  src/FreeAge/client/map.cpp
  src/FreeAge/client/opengl.cpp
  src/FreeAge/client/render_window.cpp
  src/FreeAge/client/server_connection.cpp
  src/FreeAge/client/shader_health_bar.cpp
  src/FreeAge/client/shader_program.cpp
  src/FreeAge/client/shader_sprite.cpp
  src/FreeAge/client/shader_terrain.cpp
  src/FreeAge/client/sprite.cpp
  src/FreeAge/client/sprite_atlas.cpp
  src/FreeAge/client/settings_dialog.cpp
  src/FreeAge/client/texture.cpp
  src/FreeAge/client/unit.cpp
  
  src/RectangleBinPack/MaxRectsBinPack.cpp
  src/RectangleBinPack/Rect.cpp
)
if (WIN32)
  set(FREEAGE_SRCS
    ${FREEAGE_SRCS}
    resources/free_age.rc
  )
endif()
add_executable(FreeAge WIN32
  ${FREEAGE_SRCS}
)
target_link_libraries(FreeAge
  FreeAgeLib
)
# There is a runtime dependency on FreeAgeServer.
add_dependencies(FreeAge
  FreeAgeServer
)


# FreeAge test
add_executable(FreeAgeTest
  src/FreeAge/test/test.cpp
)
target_link_libraries(FreeAgeTest
  FreeAgeLib
  gtest
)
add_test(FreeAgeTest
  FreeAgeTest
)
