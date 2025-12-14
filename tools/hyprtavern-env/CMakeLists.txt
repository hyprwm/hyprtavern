cmake_minimum_required(VERSION 3.19)

file(GLOB_RECURSE ENV_SRCFILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/protocols/*.cpp")

add_executable(hyprtavern-env ${ENV_SRCFILES})

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
  target_sources(hyprtavern-env PRIVATE protocols/${protoName}-client.cpp
                                   protocols/${protoName}-client.hpp
                                   )
endfunction()

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_kv_store_v1 false)

target_include_directories(hyprtavern-env PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/protocols")

target_link_libraries(hyprtavern-env PkgConfig::deps)

install(TARGETS hyprtavern-env)