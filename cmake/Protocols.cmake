pkg_get_variable(HYPRWIRE_PROTOCOLS_DIR hyprwire-protocols pkgdatadir)
find_program(HYPRWIRE_SCANNER_EXECUTABLE NAMES hyprwire-scanner REQUIRED)
message(STATUS "Found hyprwire-protocols at ${HYPRWIRE_PROTOCOLS_DIR}")

# hyprwire_protocol(<target> <protocol name> <SERVER|CLIENT>)
# generates bindings for a hyprtavern protocol into the calling directory's build tree.
function(hyprwire_protocol target protocol side)
  set(xml "${HYPRWIRE_PROTOCOLS_DIR}/hyprtavern/${protocol}.xml")
  set(outDir "${CMAKE_CURRENT_BINARY_DIR}/protocols")

  if(side STREQUAL "CLIENT")
    set(suffix "client")
    set(flags "--client")
  elseif(side STREQUAL "SERVER")
    set(suffix "server")
    set(flags "")
  else()
    message(FATAL_ERROR "hyprwire_protocol: side must be SERVER or CLIENT, got ${side}")
  endif()

  file(MAKE_DIRECTORY "${outDir}")

  add_custom_command(
    OUTPUT "${outDir}/${protocol}-${suffix}.cpp" "${outDir}/${protocol}-${suffix}.hpp"
    COMMAND "${HYPRWIRE_SCANNER_EXECUTABLE}" ${flags} "${xml}" "${outDir}"
    DEPENDS "${xml}" "${HYPRWIRE_SCANNER_EXECUTABLE}"
    VERBATIM)

  target_sources(${target} PRIVATE "${outDir}/${protocol}-${suffix}.cpp" "${outDir}/${protocol}-${suffix}.hpp")
  target_include_directories(${target} PUBLIC "${outDir}")
endfunction()
