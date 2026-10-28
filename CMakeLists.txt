cmake_minimum_required(VERSION 3.20)

project(SakuraCom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ftxui CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSERIALPORT REQUIRED IMPORTED_TARGET libserialport)

add_executable(sakuraCom
  src/main.cpp
  inc/ui.hpp
  src/ui.cpp
  inc/serial.hpp
  src/serial.cpp
)


target_link_libraries(sakuraCom PRIVATE
  ftxui::component
  ftxui::dom
  ftxui::screen
  PkgConfig::LIBSERIALPORT
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(sakuraCom PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(
    NAME sakuraCom.smoke
    COMMAND $<TARGET_FILE:sakuraCom>
  )
endif()
