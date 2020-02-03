cmake_minimum_required(VERSION 3.0)

project(FreeAge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# External dependency: Qt 5
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
# Instruct CMake to run rcc (resource compiler) automatically when needed.
set(CMAKE_AUTORCC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(FreeAge
  src/FreeAge/main.cc
)
target_compile_options(FreeAge PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-O2>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-msse2>"
  ";$<$<COMPILE_LANGUAGE:CXX>:-msse3>"
)
target_link_libraries(FreeAge
  Qt5::Widgets
  # Threads::Threads
  stdc++fs
)
