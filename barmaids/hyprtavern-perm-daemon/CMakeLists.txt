pkg_check_modules(
  pd_deps
  REQUIRED
  IMPORTED_TARGET
  hyprtoolkit
  pixman-1
  libdrm
  xkbcommon
)

# everything that doesn't need a display, as a library so tests can link it
set(PD_CORE_SRCFILES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/Transaction.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Text.cpp")
add_library(hyprtavern_pd_core STATIC ${PD_CORE_SRCFILES})
target_include_directories(hyprtavern_pd_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprtavern_pd_core PUBLIC hyprtavern_common)

file(GLOB_RECURSE PD_SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM PD_SRCFILES ${PD_CORE_SRCFILES})

add_executable(hyprtavern-perm-daemon ${PD_SRCFILES})
target_link_libraries(hyprtavern-perm-daemon hyprtavern_pd_core hyprtavern_barmaid PkgConfig::pd_deps)

hyprwire_protocol(hyprtavern-perm-daemon hp_hyprtavern_permission_authentication_v1 SERVER)

install(TARGETS hyprtavern-perm-daemon)
