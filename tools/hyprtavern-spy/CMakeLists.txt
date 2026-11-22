add_executable(hyprtavern-spy src/main.cpp)
target_link_libraries(hyprtavern-spy hyprtavern-client)

install(TARGETS hyprtavern-spy)
