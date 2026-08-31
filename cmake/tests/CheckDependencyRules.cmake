if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED TEST_BINARY_ROOT)
    message(FATAL_ERROR "TEST_BINARY_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_BINARY_ROOT}")
file(MAKE_DIRECTORY "${TEST_BINARY_ROOT}/source")

set(architecture_module "${SOURCE_ROOT}/cmake/CoraxArchitecture.cmake")
set(
    fixture_cmake_template
    [=[
cmake_minimum_required(VERSION 3.28)
project(CoraxDependencyFixture LANGUAGES CXX)
include("@architecture_module@")

add_library(Qt6::QmlPrivate INTERFACE IMPORTED)
add_library(Qt6::UnexpectedOne INTERFACE IMPORTED)
add_library(Qt6::UnexpectedTwo INTERFACE IMPORTED)
add_library(corax_fixture STATIC empty.cpp)

option(INCLUDE_UNEXPECTED "Include dependencies that the guard must reject" OFF)
if(INCLUDE_UNEXPECTED)
    target_link_libraries(
        corax_fixture
        PRIVATE Qt6::QmlPrivate Qt6::UnexpectedOne Qt6::UnexpectedTwo
    )
else()
    target_link_libraries(corax_fixture PRIVATE Qt6::QmlPrivate)
endif()

corax_assert_corax_dependencies(corax_fixture QT_GENERATED QmlPrivate)
]=]
)
string(CONFIGURE "${fixture_cmake_template}" fixture_cmake @ONLY)
file(WRITE "${TEST_BINARY_ROOT}/source/CMakeLists.txt" "${fixture_cmake}")
file(WRITE "${TEST_BINARY_ROOT}/source/empty.cpp" "void corax_dependency_fixture() {}\n")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${TEST_BINARY_ROOT}/source"
        -B "${TEST_BINARY_ROOT}/allowed"
        -DINCLUDE_UNEXPECTED=OFF
    RESULT_VARIABLE allowed_result
    OUTPUT_VARIABLE allowed_output
    ERROR_VARIABLE allowed_error
)
if(NOT allowed_result EQUAL 0)
    message(
        FATAL_ERROR
        "Qt-generated dependency fixture unexpectedly failed:\n${allowed_output}${allowed_error}"
    )
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${TEST_BINARY_ROOT}/source"
        -B "${TEST_BINARY_ROOT}/rejected"
        -DINCLUDE_UNEXPECTED=ON
    RESULT_VARIABLE rejected_result
    OUTPUT_VARIABLE rejected_output
    ERROR_VARIABLE rejected_error
)
if(rejected_result EQUAL 0)
    message(FATAL_ERROR "Unexpected dependency fixture passed")
endif()

string(CONCAT rejected_log "${rejected_output}" "${rejected_error}")
foreach(expected_dependency IN ITEMS Qt6::UnexpectedOne Qt6::UnexpectedTwo)
    string(FIND "${rejected_log}" "${expected_dependency}" dependency_index)
    if(dependency_index EQUAL -1)
        message(
            FATAL_ERROR
            "Dependency report omitted ${expected_dependency}:\n${rejected_log}"
        )
    endif()
endforeach()
