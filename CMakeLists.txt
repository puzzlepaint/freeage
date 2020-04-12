cmake_minimum_required(VERSION 3.0)

cmake_policy(SET CMP0054 NEW)

project(FreeAge)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)


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
if (UNIX)
  find_package(Qt5 REQUIRED COMPONENTS X11Extras)
endif()


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
if (WIN32)
  target_compile_definitions(loguru PRIVATE
    LOGURU_EXPORTS
  )
endif()
target_link_libraries(loguru
  Threads::Threads
  ${CMAKE_DL_LIBS}
)


# Packaged dependency: yaml-cpp (for json file loading)
add_subdirectory(third_party/yaml-cpp-0.6.0)


# Packaged dependency: googletest (for unit tests)
add_subdirectory(third_party/googletest)
set_target_properties(gtest gtest_main PROPERTIES FOLDER "third_party")


# FreeAge base library, used by both the application and the test
# TODO: Unify cpp / cc spelling. Maybe also use hpp to make it clear for headers too that they are C++ code, not C code.
add_library(FreeAgeLib
  src/FreeAge/common/building_types.cpp
  src/FreeAge/common/messages.cpp
  src/FreeAge/common/timing.cpp
  src/FreeAge/common/unit_types.cpp
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(FreeAgeLib PUBLIC
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
    ";$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
  )
endif()
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(FreeAgeLib PUBLIC
    ";$<$<COMPILE_LANGUAGE:CXX>:-O2>"
    ";$<$<COMPILE_LANGUAGE:CXX>:-msse2>"
    ";$<$<COMPILE_LANGUAGE:CXX>:-msse3>"
    ";$<$<COMPILE_LANGUAGE:CXX>:-Wno-comment>"
  )
endif()
target_include_directories(FreeAgeLib PUBLIC
  src
  third_party
  third_party/mango/include
  third_party/yaml-cpp-0.6.0/include
)
target_link_libraries(FreeAgeLib PUBLIC
  Qt5::Widgets
  Qt5::Network
  mango
  yaml-cpp2
  loguru
  Threads::Threads
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
target_link_libraries(FreeAgeLib PUBLIC
  stdc++fs  # TODO: I think this is specific to gcc version 8
)
endif()


# FreeAge server application
add_executable(FreeAgeServer
  src/FreeAge/server/building.cpp
  src/FreeAge/server/game.cpp
  src/FreeAge/server/main.cpp
  src/FreeAge/server/map.cpp
  src/FreeAge/server/match_setup.cpp
  src/FreeAge/server/object.cpp
  src/FreeAge/server/unit.cpp
)
target_link_libraries(FreeAgeServer
  FreeAgeLib
)


# FreeAge application
set(FREEAGE_SRCS
  resources/resources.qrc
  src/FreeAge/client/about_dialog.cpp
  src/FreeAge/client/building.cpp
  src/FreeAge/client/command_button.cpp
  src/FreeAge/client/decal.cpp
  src/FreeAge/client/game_controller.cpp
  src/FreeAge/client/lobby_dialog.cpp
  src/FreeAge/client/health_bar.cpp
  src/FreeAge/client/main.cpp
  src/FreeAge/client/map.cpp
  src/FreeAge/client/match.cpp
  src/FreeAge/client/mod_manager.cpp
  src/FreeAge/client/object.cpp
  src/FreeAge/client/opaqueness_map.cpp
  src/FreeAge/client/opengl.cpp
  src/FreeAge/client/render_utils.cpp
  src/FreeAge/client/render_window.cpp
  src/FreeAge/client/server_connection.cpp
  src/FreeAge/client/shader_color_dilation.cpp
  src/FreeAge/client/shader_health_bar.cpp
  src/FreeAge/client/shader_program.cpp
  src/FreeAge/client/shader_sprite.cpp
  src/FreeAge/client/shader_terrain.cpp
  src/FreeAge/client/shader_ui.cpp
  src/FreeAge/client/shader_ui_single_color.cpp
  src/FreeAge/client/shader_ui_single_color_fullscreen.cpp
  src/FreeAge/client/sprite.cpp
  src/FreeAge/client/sprite_atlas.cpp
  src/FreeAge/client/text_display.cpp
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
target_link_libraries(FreeAge PUBLIC
  FreeAgeLib
)
if (UNIX)
  target_link_libraries(FreeAge PUBLIC
    Qt5::X11Extras
    X11
  )
  target_compile_options(FreeAge PUBLIC
    ";$<$<COMPILE_LANGUAGE:CXX>:-DHAVE_X11_EXTRAS>"
  )
endif()
# There is a runtime dependency on FreeAgeServer.
add_dependencies(FreeAge
  FreeAgeServer
)


# FreeAge test
add_executable(FreeAgeTest
  src/FreeAge/test/test.cpp
  
  src/FreeAge/client/map.cpp
  src/FreeAge/client/mod_manager.cpp
  src/FreeAge/client/opengl.cpp
  src/FreeAge/client/shader_program.cpp
  src/FreeAge/client/shader_terrain.cpp
)
target_link_libraries(FreeAgeTest
  FreeAgeLib
  gtest
)
add_test(FreeAgeTest
  FreeAgeTest
)
