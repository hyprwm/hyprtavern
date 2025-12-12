cmake_minimum_required(VERSION 3.19)

file(GLOB_RECURSE KV_SRCFILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/protocols/*.cpp")

add_executable(hyprtavern-kv ${KV_SRCFILES})

pkg_check_modules(
  kv_deps
  REQUIRED
  IMPORTED_TARGET
  openssl
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
  target_sources(hyprtavern-kv PRIVATE protocols/${protoName}-server.cpp
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
  target_sources(hyprtavern-kv PRIVATE protocols/${protoName}-client.cpp
                                   protocols/${protoName}-client.hpp
                                   )
endfunction()

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_kv_store_v1 false)

target_include_directories(hyprtavern-kv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/protocols")

target_link_libraries(hyprtavern-kv PkgConfig::deps PkgConfig::kv_deps)

install(TARGETS hyprtavern-kv)