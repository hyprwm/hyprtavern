add_executable(hyprtavern-spy src/main.cpp)
target_link_libraries(hyprtavern-spy PkgConfig::deps)

hyprwire_protocol(hyprtavern-spy hp_hyprtavern_core_v1 CLIENT)
hyprwire_protocol(hyprtavern-spy hp_hyprtavern_kv_store_v1 CLIENT)

install(TARGETS hyprtavern-spy)
