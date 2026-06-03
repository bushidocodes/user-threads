cmake_minimum_required(VERSION 3.14)
project(user_threads C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /wd4996)
else()
    add_compile_options(-Wall -Wextra -g)
endif()

# Unity source is vendored under unity/
add_executable(test_gwthd
    test_gwthd.c
    helper_tu.c
    gwthd.c
    unity/unity.c
)

target_include_directories(test_gwthd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/unity
)

enable_testing()
add_test(NAME gwthd_tests COMMAND test_gwthd)
