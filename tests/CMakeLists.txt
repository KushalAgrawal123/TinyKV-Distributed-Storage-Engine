find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(tinykv_tests
    test_storage.cpp
    test_cache.cpp
    test_networking.cpp
    test_concurrency.cpp
    test_persistence.cpp
)

target_link_libraries(tinykv_tests PRIVATE tinykv_core GTest::gtest GTest::gtest_main)
target_compile_options(tinykv_tests PRIVATE -Wall -Wextra -Wpedantic)

gtest_discover_tests(tinykv_tests)
