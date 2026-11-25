add_executable(test_util
    test_util.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c)
target_include_directories(test_util PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_util PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_util COMMAND test_util)
