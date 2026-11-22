file(GLOB_RECURSE CLIENT_SRCFILES CONFIGURE_DEPENDS "src/*.cpp")

add_library(hyprtavern-client SHARED ${CLIENT_SRCFILES})
target_include_directories(
  hyprtavern-client
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprtavern-client PUBLIC PkgConfig::deps PRIVATE hyprtavern_common)

# only the public API is exported, so the bindings we generate never clash with an app's own
set_target_properties(
  hyprtavern-client
  PROPERTIES VERSION ${HYPRTAVERN_VERSION}
             SOVERSION 0
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON)

hyprwire_protocol(hyprtavern-client hp_hyprtavern_core_v1 CLIENT)
hyprwire_protocol(hyprtavern-client hp_hyprtavern_kv_store_v1 CLIENT)

set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(INCLUDE ${CMAKE_INSTALL_FULL_INCLUDEDIR})
set(LIBDIR ${CMAKE_INSTALL_FULL_LIBDIR})
configure_file(hyprtavern-client.pc.in hyprtavern-client.pc @ONLY)

install(TARGETS hyprtavern-client)
install(DIRECTORY "include/hyprtavern-client" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hyprtavern-client.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
