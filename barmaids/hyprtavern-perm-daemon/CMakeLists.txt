cmake_minimum_required(VERSION 3.19)

file(GLOB_RECURSE PD_SRCFILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/protocols/*.cpp")

add_executable(hyprtavern-perm-daemon ${PD_SRCFILES})

pkg_check_modules(
  pd_deps
  REQUIRED
  IMPORTED_TARGET
  hyprtoolkit
  pixman-1
  libdrm
)

function(hyprprotocolServer protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protoName}-server.cpp
           ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protoName}-server.hpp
    COMMAND hyprwire-scanner ${path}/${protoName}.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(hyprtavern-perm-daemon PRIVATE protocols/${protoName}-server.cpp
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
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protoName}-client.cpp
           ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protoName}-client.hpp
    COMMAND hyprwire-scanner --client ${path}/${protoName}.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(hyprtavern-perm-daemon PRIVATE protocols/${protoName}-client.cpp
                                   protocols/${protoName}-client.hpp
                                   )
endfunction()

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_permission_authentication_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_barmaid_v1 false)

target_include_directories(hyprtavern-perm-daemon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/protocols")

target_link_libraries(hyprtavern-perm-daemon PkgConfig::deps PkgConfig::pd_deps)

install(TARGETS hyprtavern-perm-daemon)