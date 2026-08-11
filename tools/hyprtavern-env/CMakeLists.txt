cmake_minimum_required(VERSION 3.19)

file(GLOB_RECURSE ENV_SRCFILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(ENV_PROTOCOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${ENV_PROTOCOLS_DIR}")

add_executable(hyprtavern-env ${ENV_SRCFILES})

function(hyprprotocol protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  set(xml "${path}/${protoName}.xml")
  add_custom_command(
    OUTPUT "${ENV_PROTOCOLS_DIR}/${protoName}-client.cpp"
           "${ENV_PROTOCOLS_DIR}/${protoName}-client.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" --client "${xml}" "${ENV_PROTOCOLS_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern-env PRIVATE
    "${ENV_PROTOCOLS_DIR}/${protoName}-client.cpp"
    "${ENV_PROTOCOLS_DIR}/${protoName}-client.hpp")
endfunction()

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocol(hyprtavern hp_hyprtavern_kv_store_v1 false)

target_include_directories(hyprtavern-env PRIVATE "${ENV_PROTOCOLS_DIR}")

target_link_libraries(hyprtavern-env PkgConfig::deps)

install(TARGETS hyprtavern-env)