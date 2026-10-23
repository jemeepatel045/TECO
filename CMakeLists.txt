cmake_minimum_required(VERSION 3.16)
project(TECO VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(teco_engine INTERFACE)
target_include_directories(teco_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(teco src/main.cpp)
target_link_libraries(teco PRIVATE teco_engine)

add_executable(teco_server src/server.cpp)
target_link_libraries(teco_server PRIVATE teco_engine)

add_executable(teco_tests tests/test_engine.cpp)
target_link_libraries(teco_tests PRIVATE teco_engine)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(teco_tests PRIVATE -UNDEBUG)
endif()

enable_testing()
add_test(NAME teco_tests COMMAND teco_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME teco_api_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.py
            $<TARGET_FILE:teco_server>
)
set_tests_properties(teco_api_tests PROPERTIES TIMEOUT 30)

add_test(
    NAME teco_redis_api_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_redis_api.py
            $<TARGET_FILE:teco_server>
)
set_tests_properties(
    teco_redis_api_tests
    PROPERTIES
        TIMEOUT 30
        SKIP_RETURN_CODE 77
)
