add_executable(tinykv-bench bench_main.cpp)
target_link_libraries(tinykv-bench PRIVATE tinykv_core)
