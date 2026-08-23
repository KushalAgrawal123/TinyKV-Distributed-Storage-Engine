set(TINYKV_CORE_SOURCES
    logger.cpp
    config.cpp
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
