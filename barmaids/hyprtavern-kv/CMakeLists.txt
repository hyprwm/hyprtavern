# the store itself, as a library so tests can link it
add_library(hyprtavern_kv_store STATIC src/store/Store.cpp)
target_include_directories(hyprtavern_kv_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hyprtavern_kv_store PUBLIC hyprtavern_common glaze::glaze)

file(GLOB_RECURSE KV_SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM KV_SRCFILES "${CMAKE_CURRENT_SOURCE_DIR}/src/store/Store.cpp")

add_executable(hyprtavern-kv ${KV_SRCFILES})
target_link_libraries(hyprtavern-kv hyprtavern_kv_store hyprtavern_barmaid)

hyprwire_protocol(hyprtavern-kv hp_hyprtavern_core_v1 CLIENT)
hyprwire_protocol(hyprtavern-kv hp_hyprtavern_kv_store_v1 SERVER)

install(TARGETS hyprtavern-kv)
