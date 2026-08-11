cmake_minimum_required(VERSION 3.19)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} HYPRTAVERN_VERSION)

add_compile_definitions(HYPRTAVERN_VERSION="${HYPRTAVERN_VERSION}")

project(
  hyprtavern
  VERSION ${HYPRTAVERN_VERSION}
  DESCRIPTION "A modern, simple and consistent session bus for IPC discovery.")

include(CTest)
include(CheckIncludeFile)
include(GNUInstallDirs)

set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(INCLUDE ${CMAKE_INSTALL_FULL_INCLUDEDIR})
set(LIBDIR ${CMAKE_INSTALL_FULL_LIBDIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  deps
  REQUIRED
  IMPORTED_TARGET
  hyprutils>=0.10.4
  hyprwire>=0.2.1
  hyprwire-protocols
  uuid
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
  add_compile_definitions(HYPRLAUNCHER_DEBUG)
  set(BUILD_TESTING ON)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprtavern in Release")
  set(BUILD_TESTING OFF)
endif()

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp" "include/*.hpp")

add_executable(hyprtavern ${SRCFILES})

pkg_get_variable(HYPRWIRE_PROTOCOLS_DIR hyprwire-protocols pkgdatadir)
find_program(HYPRWIRE_SCANNER_EXECUTABLE NAMES hyprwire-scanner REQUIRED)
set(HYPRWIRE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${HYPRWIRE_GENERATED_DIR}")
message(STATUS "Found hyprwire-protocols at ${HYPRWIRE_PROTOCOLS_DIR}")

function(hyprprotocolServer protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  set(xml "${path}/${protoName}.xml")
  add_custom_command(
    OUTPUT "${HYPRWIRE_GENERATED_DIR}/${protoName}-server.cpp"
           "${HYPRWIRE_GENERATED_DIR}/${protoName}-server.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" "${xml}" "${HYPRWIRE_GENERATED_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern PRIVATE
    "${HYPRWIRE_GENERATED_DIR}/${protoName}-server.cpp"
    "${HYPRWIRE_GENERATED_DIR}/${protoName}-server.hpp")
endfunction()

function(hyprprotocol protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  set(xml "${path}/${protoName}.xml")
  add_custom_command(
    OUTPUT "${HYPRWIRE_GENERATED_DIR}/${protoName}-client.cpp"
           "${HYPRWIRE_GENERATED_DIR}/${protoName}-client.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" --client "${xml}" "${HYPRWIRE_GENERATED_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern PRIVATE
    "${HYPRWIRE_GENERATED_DIR}/${protoName}-client.cpp"
    "${HYPRWIRE_GENERATED_DIR}/${protoName}-client.hpp")
endfunction()

hyprprotocolServer(hyprtavern hp_hyprtavern_core_v1 false)

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_kv_store_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_barmaid_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_permission_authentication_v1 false)

target_include_directories(hyprtavern PRIVATE "${HYPRWIRE_GENERATED_DIR}")

target_link_libraries(hyprtavern PkgConfig::deps glaze::glaze)

add_subdirectory(tools/hyprtavern-spy)
add_subdirectory(tools/hyprtavern-env)
add_subdirectory(barmaids/hyprtavern-kv)
add_subdirectory(barmaids/hyprtavern-perm-daemon)

if(BUILD_TESTING)
  message(STATUS "building tests is enabled")

  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)
  file(GLOB_RECURSE TESTFILES CONFIGURE_DEPENDS "tests/*.cpp")
  add_executable(hyprtavern_tests
    ${TESTFILES}
    src/security/Permissions.cpp
    "${HYPRWIRE_GENERATED_DIR}/hp_hyprtavern_core_v1-server.hpp")
  target_include_directories(hyprtavern_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${HYPRWIRE_GENERATED_DIR}")
  target_link_libraries(hyprtavern_tests PRIVATE GTest::gtest_main PkgConfig::deps)
  gtest_discover_tests(hyprtavern_tests)
else()
  message(STATUS "building tests is disabled")
endif()

install(TARGETS hyprtavern)
