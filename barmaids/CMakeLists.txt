# shared between all barmaids
add_library(hyprtavern_barmaid STATIC shared/BarmaidServer.cpp)
target_include_directories(hyprtavern_barmaid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hyprtavern_barmaid PUBLIC hyprtavern_common)
hyprwire_protocol(hyprtavern_barmaid hp_hyprtavern_barmaid_v1 SERVER)

add_subdirectory(hyprtavern-kv)
