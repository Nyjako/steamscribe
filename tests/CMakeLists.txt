add_executable(test_util
    test_util.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c)
target_include_directories(test_util PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_util PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_util COMMAND test_util)

add_executable(test_steam
    test_steam.c
    ${CMAKE_SOURCE_DIR}/src/steam/steam.c
    ${CMAKE_SOURCE_DIR}/src/net/http.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c)
target_include_directories(test_steam PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_steam PRIVATE cjson CURL::libcurl goblin)
target_compile_definitions(test_steam PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_options(test_steam PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_steam COMMAND test_steam)

add_executable(test_config
    test_config.c
    ${CMAKE_SOURCE_DIR}/src/config.c
    ${CMAKE_SOURCE_DIR}/src/steam/cache.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c)
target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_config PRIVATE inih goblin)
target_compile_definitions(test_config PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_config_test")
target_compile_options(test_config PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_config COMMAND test_config)

add_executable(test_themes
    test_themes.c
    ${CMAKE_SOURCE_DIR}/src/core/themes.c
    ${CMAKE_SOURCE_DIR}/src/llm/fake.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c)
target_include_directories(test_themes PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_themes PRIVATE cjson)
target_compile_options(test_themes PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_themes COMMAND test_themes)


