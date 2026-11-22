add_executable(hyprtavern-env src/main.cpp)
target_link_libraries(hyprtavern-env hyprtavern-client)

install(TARGETS hyprtavern-env)
