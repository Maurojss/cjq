cmake_minimum_required(VERSION 3.13)
project(cjq C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(cjq
    src/main.c
    src/json.c
    src/query.c
)
target_link_libraries(cjq PRIVATE m)
target_compile_options(cjq PRIVATE -Wall -Wextra -Wpedantic)

add_executable(unit tests/unit.c src/json.c src/query.c)
target_include_directories(unit PRIVATE src)
target_link_libraries(unit PRIVATE m)

enable_testing()

add_test(NAME unit COMMAND unit)
add_test(NAME e2e
    COMMAND ${CMAKE_COMMAND} -E env CJQ_BIN=$<TARGET_FILE:cjq>
            sh ${CMAKE_SOURCE_DIR}/tests/run_tests.sh)

install(TARGETS cjq RUNTIME DESTINATION bin)

if(UNIX)
    install(FILES man/cjq.1 DESTINATION share/man/man1)
    install(FILES completions/cjq.bash
            DESTINATION share/bash-completion/completions RENAME cjq)
    install(FILES completions/_cjq DESTINATION share/zsh/site-functions)
    install(FILES completions/cjq.fish
            DESTINATION share/fish/vendor_completions.d)
endif()
