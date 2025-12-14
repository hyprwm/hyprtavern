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
  hyprlang
  uuid
)

find_package(glaze 6.0.0 QUIET)
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
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprtavern in Release")
endif()

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp" "protocols/*.cpp" "include/*.hpp")

add_executable(hyprtavern ${SRCFILES})

pkg_get_variable(HYPRWIRE_PROTOCOLS_DIR hyprwire-protocols pkgdatadir)
message(STATUS "Found hyprwire-protocols at ${HYPRWIRE_PROTOCOLS_DIR}")

function(hyprprotocolServer protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/${protoName}-server.cpp
           ${CMAKE_SOURCE_DIR}/protocols/${protoName}-server.hpp
    COMMAND hyprwire-scanner ${path}/${protoName}.xml
            ${CMAKE_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_sources(hyprtavern PRIVATE protocols/${protoName}-server.cpp
                                   protocols/${protoName}-server.hpp
                                   )
endfunction()

function(hyprprotocol protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/${protoName}-client.cpp
           ${CMAKE_SOURCE_DIR}/protocols/${protoName}-client.hpp
    COMMAND hyprwire-scanner --client ${path}/${protoName}.xml
            ${CMAKE_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_sources(hyprtavern PRIVATE protocols/${protoName}-client.cpp
                                   protocols/${protoName}-client.hpp
                                   )
endfunction()

hyprprotocolServer(hyprtavern hp_hyprtavern_core_v1 false)

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_kv_store_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_barmaid_v1 false)

target_include_directories(hyprtavern PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/protocols")

target_link_libraries(hyprtavern PkgConfig::deps)

add_subdirectory(tools/hyprtavern-spy)
add_subdirectory(tools/hyprtavern-env)
add_subdirectory(barmaids/hyprtavern-kv)

install(TARGETS hyprtavern)