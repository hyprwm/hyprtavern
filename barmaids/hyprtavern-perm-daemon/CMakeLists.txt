cmake_minimum_required(VERSION 3.19)

file(GLOB_RECURSE PD_SRCFILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(PD_PROTOCOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PD_PROTOCOLS_DIR}")

add_executable(hyprtavern-perm-daemon ${PD_SRCFILES})

pkg_check_modules(
  pd_deps
  REQUIRED
  IMPORTED_TARGET
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
    OUTPUT "${PD_PROTOCOLS_DIR}/${protoName}-server.cpp"
           "${PD_PROTOCOLS_DIR}/${protoName}-server.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" "${xml}" "${PD_PROTOCOLS_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern-perm-daemon PRIVATE
    "${PD_PROTOCOLS_DIR}/${protoName}-server.cpp"
    "${PD_PROTOCOLS_DIR}/${protoName}-server.hpp")
endfunction()

function(hyprprotocol protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${HYPRWIRE_PROTOCOLS_DIR}/${protoPath})
  endif()
  set(xml "${path}/${protoName}.xml")
  add_custom_command(
    OUTPUT "${PD_PROTOCOLS_DIR}/${protoName}-client.cpp"
           "${PD_PROTOCOLS_DIR}/${protoName}-client.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" --client "${xml}" "${PD_PROTOCOLS_DIR}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)
  target_sources(hyprtavern-perm-daemon PRIVATE
    "${PD_PROTOCOLS_DIR}/${protoName}-client.cpp"
    "${PD_PROTOCOLS_DIR}/${protoName}-client.hpp")
endfunction()

hyprprotocol(hyprtavern hp_hyprtavern_core_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_permission_authentication_v1 false)
hyprprotocolServer(hyprtavern hp_hyprtavern_barmaid_v1 false)

target_include_directories(hyprtavern-perm-daemon PRIVATE "${PD_PROTOCOLS_DIR}" ${pd_deps_INCLUDE_DIRS})

target_link_libraries(hyprtavern-perm-daemon PkgConfig::deps PkgConfig::pd_deps)

install(TARGETS hyprtavern-perm-daemon)
