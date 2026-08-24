set(TINYKV_CORE_SOURCES
    logger.cpp
    config.cpp
    storage/kv_store.cpp
    storage/expiry_manager.cpp
    net/line_protocol.cpp
    net/tcp_server.cpp
    net/tcp_client.cpp
    protocol/parser.cpp
    protocol/reply.cpp
    command_executor.cpp
    concurrency/thread_pool.cpp
    persistence/replay.cpp
    persistence/aof.cpp
    persistence/snapshot.cpp
    persistence/persistence_manager.cpp
    replication/replica_registry.cpp
    replication/replica_link.cpp
    replication/replication_manager.cpp
    sharding/consistent_hash_ring.cpp
    sharding/router.cpp
)

add_library(tinykv_core STATIC ${TINYKV_CORE_SOURCES})

target_include_directories(tinykv_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(tinykv_core PUBLIC Threads::Threads)

target_compile_options(tinykv_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(tinykv-server server_main.cpp)
target_link_libraries(tinykv-server PRIVATE tinykv_core)

add_executable(tinykv-cli cli_main.cpp)
target_link_libraries(tinykv-cli PRIVATE tinykv_core)

add_executable(tinykv-router router_main.cpp)
target_link_libraries(tinykv-router PRIVATE tinykv_core)
