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
    ${CMAKE_SOURCE_DIR}/src/util/error.c
    ${CMAKE_SOURCE_DIR}/src/util/json_fence.c)
target_include_directories(test_themes PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_themes PRIVATE cjson)
target_compile_options(test_themes PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_themes COMMAND test_themes)

add_executable(test_review
    test_review.c
    ${CMAKE_SOURCE_DIR}/src/core/review.c
    ${CMAKE_SOURCE_DIR}/src/llm/fake.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c)
target_include_directories(test_review PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_review PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_review COMMAND test_review)

add_executable(test_followup
    test_followup.c
    ${CMAKE_SOURCE_DIR}/src/core/followup.c
    ${CMAKE_SOURCE_DIR}/src/llm/fake.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c
    ${CMAKE_SOURCE_DIR}/src/util/json_fence.c)
target_include_directories(test_followup PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_followup PRIVATE cjson)
target_compile_options(test_followup PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_followup COMMAND test_followup)

add_executable(test_spoiler_predict
    test_spoiler_predict.c
    ${CMAKE_SOURCE_DIR}/src/core/spoiler_predict.c
    ${CMAKE_SOURCE_DIR}/src/llm/fake.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c
    ${CMAKE_SOURCE_DIR}/src/util/json_fence.c)
target_include_directories(test_spoiler_predict PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_spoiler_predict PRIVATE cjson)
target_compile_options(test_spoiler_predict PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_spoiler_predict COMMAND test_spoiler_predict)

add_executable(test_text
    test_text.c
    ${CMAKE_SOURCE_DIR}/src/util/text.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c)
target_include_directories(test_text PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_text PRIVATE goblin)
target_compile_options(test_text PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_text COMMAND test_text)

add_executable(test_pipeline
    test_pipeline.c
    ${CMAKE_SOURCE_DIR}/src/core/pipeline.c
    ${CMAKE_SOURCE_DIR}/src/core/themes.c
    ${CMAKE_SOURCE_DIR}/src/llm/llm.c
    ${CMAKE_SOURCE_DIR}/src/llm/anthropic.c
    ${CMAKE_SOURCE_DIR}/src/llm/openai.c
    ${CMAKE_SOURCE_DIR}/src/llm/ollama.c
    ${CMAKE_SOURCE_DIR}/src/llm/fake.c
    ${CMAKE_SOURCE_DIR}/src/net/http.c
    ${CMAKE_SOURCE_DIR}/src/steam/cache.c
    ${CMAKE_SOURCE_DIR}/src/steam/steam.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c
    ${CMAKE_SOURCE_DIR}/src/util/debug_log.c
    ${CMAKE_SOURCE_DIR}/src/util/json_fence.c
    ${CMAKE_SOURCE_DIR}/src/util/text.c)
target_include_directories(test_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_pipeline PRIVATE cjson CURL::libcurl goblin)
target_compile_options(test_pipeline PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_pipeline COMMAND test_pipeline)

add_executable(test_tui_state
    test_tui_state.c
    ${CMAKE_SOURCE_DIR}/src/tui/state.c
    ${CMAKE_SOURCE_DIR}/src/core/themes.c
    ${CMAKE_SOURCE_DIR}/src/util/arena.c
    ${CMAKE_SOURCE_DIR}/src/util/strbuf.c
    ${CMAKE_SOURCE_DIR}/src/util/error.c
    ${CMAKE_SOURCE_DIR}/src/util/json_fence.c)
target_include_directories(test_tui_state PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${cjson_SOURCE_DIR})
target_link_libraries(test_tui_state PRIVATE cjson)
target_compile_options(test_tui_state PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_tui_state COMMAND test_tui_state)





