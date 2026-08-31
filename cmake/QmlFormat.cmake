if(NOT DEFINED QMLFORMAT_EXECUTABLE OR NOT DEFINED QML_ROOT OR NOT DEFINED MODE)
    message(FATAL_ERROR "QMLFORMAT_EXECUTABLE, QML_ROOT, and MODE are required")
endif()

file(GLOB_RECURSE qml_files "${QML_ROOT}/*.qml")
foreach(qml_file IN LISTS qml_files)
    if(MODE STREQUAL "format")
        execute_process(
            COMMAND "${QMLFORMAT_EXECUTABLE}" -i "${qml_file}"
            RESULT_VARIABLE format_result
            ERROR_VARIABLE format_error
        )
    elseif(MODE STREQUAL "check")
        file(READ "${qml_file}" original_content)
        execute_process(
            COMMAND "${QMLFORMAT_EXECUTABLE}" "${qml_file}"
            RESULT_VARIABLE format_result
            OUTPUT_VARIABLE formatted_content
            ERROR_VARIABLE format_error
        )
        if(format_result EQUAL 0 AND NOT original_content STREQUAL formatted_content)
            message(FATAL_ERROR "QML formatting differs: ${qml_file}")
        endif()
    else()
        message(FATAL_ERROR "Unknown QML formatting mode: ${MODE}")
    endif()

    if(NOT format_result EQUAL 0)
        message(FATAL_ERROR "qmlformat failed for ${qml_file}: ${format_error}")
    endif()
endforeach()
