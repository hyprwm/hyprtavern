add_executable(hyprtavern-env src/main.cpp)
target_link_libraries(hyprtavern-env PkgConfig::deps)

hyprwire_protocol(hyprtavern-env hp_hyprtavern_core_v1 CLIENT)

install(TARGETS hyprtavern-env)
