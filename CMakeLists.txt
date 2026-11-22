cmake_minimum_required(VERSION 3.19)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} HYPRTAVERN_VERSION)

add_compile_definitions(HYPRTAVERN_VERSION="${HYPRTAVERN_VERSION}")

project(
  hyprtavern
  VERSION ${HYPRTAVERN_VERSION}
  DESCRIPTION "A modern, simple and consistent session bus for IPC discovery.")

include(CTest)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  deps
  REQUIRED
  IMPORTED_TARGET
  hyprutils>=0.10.4
  hyprwire>=0.3.1
  hyprwire-protocols
)

find_package(glaze CONFIG QUIET)
if (NOT glaze_FOUND)
    set(GLAZE_VERSION v6.1.0)
    message(STATUS "glaze dependency not found, retrieving ${GLAZE_VERSION} with FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG ${GLAZE_VERSION}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glaze)
endif()

set(CMAKE_CXX_STANDARD 23)
add_compile_options(
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wpedantic)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring hyprtavern in Debug")
  add_compile_definitions(HYPRTAVERN_DEBUG)
  set(BUILD_TESTING ON)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprtavern in Release")
  set(BUILD_TESTING OFF)
endif()

include(cmake/Protocols.cmake)

# shared between the tavern, barmaids and tools
file(GLOB COMMON_SRCFILES CONFIGURE_DEPENDS "common/*.cpp")
add_library(hyprtavern_common STATIC ${COMMON_SRCFILES})
target_include_directories(hyprtavern_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hyprtavern_common PUBLIC PkgConfig::deps)

if(BUILD_TESTING)
  message(STATUS "building tests is enabled")

  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)
  file(GLOB_RECURSE TESTFILES CONFIGURE_DEPENDS "tests/*.cpp")
  add_executable(hyprtavern_tests ${TESTFILES})
  target_include_directories(hyprtavern_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(hyprtavern_tests PRIVATE hyprtavern_common GTest::gtest_main)
  gtest_discover_tests(hyprtavern_tests)
else()
  message(STATUS "building tests is disabled")
endif()
