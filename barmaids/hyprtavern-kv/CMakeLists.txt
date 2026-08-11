cmake_minimum_required(VERSION 3.19)

file(GLOB_RECURSE KV_SRCFILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KV_PROTOCOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${KV_PROTOCOLS_DIR}")

add_executable(hyprtavern-kv ${KV_SRCFILES})

pkg_check_modules(
  kv_deps
  REQUIRED
  IMPORTED_TARGET
  openssl
  hyprtoolkit
  pixman-1
  libdrm
  xkbcommon
)

function(hyprprotocolServer protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  set(xml "${path}/${protoName}.xml")
  add_custom_command(
    OUTPUT "${KV_PROTOCOLS_DIR}/${protoName}-server.cpp"
           "${KV_PROTOCOLS_DIR}/${protoName}-server.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" "${xml}" "${KV_PROTOCOLS_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern-kv PRIVATE
    "${KV_PROTOCOLS_DIR}/${protoName}-server.cpp"
    "${KV_PROTOCOLS_DIR}/${protoName}-server.hpp")
endfunction()

function(hyprprotocol protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  set(xml "${path}/${protoName}.xml")
  add_custom_command(
    OUTPUT "${KV_PROTOCOLS_DIR}/${protoName}-client.cpp"
           "${KV_PROTOCOLS_DIR}/${protoName}-client.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" --client "${xml}" "${KV_PROTOCOLS_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern-kv PRIVATE
    "${KV_PROTOCOLS_DIR}/${protoName}-client.cpp"
    "${KV_PROTOCOLS_DIR}/${protoName}-client.hpp")
endfunction()

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_kv_store_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_barmaid_v1 false)

target_include_directories(hyprtavern-kv PRIVATE "${KV_PROTOCOLS_DIR}" ${kv_deps_INCLUDE_DIRS})

target_link_libraries(hyprtavern-kv PkgConfig::deps PkgConfig::kv_deps glaze::glaze)

install(TARGETS hyprtavern-kv)
