if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED TEST_BINARY_ROOT)
    message(FATAL_ERROR "TEST_BINARY_ROOT is required")
endif()

function(run_boundary_case name relative_path source_text expect_success expected_message)
    set(case_root "${TEST_BINARY_ROOT}/${name}")
    get_filename_component(case_directory "${case_root}/${relative_path}" DIRECTORY)
    file(REMOVE_RECURSE "${case_root}")
    file(MAKE_DIRECTORY "${case_directory}")
    file(WRITE "${case_root}/${relative_path}" "${source_text}")

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${case_root}"
            -P "${SOURCE_ROOT}/cmake/CheckBoundaries.cmake"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    string(CONCAT combined_output "${standard_output}" "${standard_error}")

    if(expect_success AND NOT result EQUAL 0)
        message(FATAL_ERROR "${name} unexpectedly failed:\n${combined_output}")
    elseif(NOT expect_success AND result EQUAL 0)
        message(FATAL_ERROR "${name} unexpectedly passed")
    endif()

    if(NOT expected_message STREQUAL "")
        string(FIND "${combined_output}" "${expected_message}" message_index)
        if(message_index EQUAL -1)
            message(
                FATAL_ERROR
                "${name} did not report '${expected_message}':\n${combined_output}"
            )
        endif()
    endif()
endfunction()

string(ASCII 9 horizontal_tab)
run_boundary_case(
    corax_internal_private_path
    src/ui/Internal.cpp
    "#include <corax/example/private/Example_p.h>\n"
    TRUE
    ""
)
run_boundary_case(
    qt_private_header_with_tab
    src/ui/QtPrivate.cpp
    "#${horizontal_tab}include <QtQml/private/qqmlengine_p.h>\n"
    FALSE
    "Qt private API header used by Corax source"
)
run_boundary_case(
    qt_versioned_private_header
    src/ui/QtVersionedPrivate.cpp
    "#include <QtQml/6.8.3/QtQml/private/qqmlengine_p.h>\n"
    FALSE
    "Qt private API header used by Corax source"
)
run_boundary_case(
    qt_private_header_in_storage
    src/storage_sqlite/QtPrivate.cpp
    "#include <private/qqmlmetatype_p.h>\n"
    FALSE
    "Qt private API header used by Corax source"
)
