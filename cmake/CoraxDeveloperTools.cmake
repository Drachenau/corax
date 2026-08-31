function(corax_add_developer_tools)
    file(
        GLOB_RECURSE corax_cpp_files
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_SOURCE_DIR}/tests/*.h"
    )
    file(
        GLOB_RECURSE corax_qml_files
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/ui/*.qml"
    )

    find_program(CORAX_CLANG_FORMAT_EXECUTABLE NAMES clang-format-18 clang-format)
    if(CORAX_CLANG_FORMAT_EXECUTABLE)
        add_custom_target(
            format
            COMMAND "${CORAX_CLANG_FORMAT_EXECUTABLE}" -i ${corax_cpp_files}
            COMMENT "Formatting Corax C++ sources"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
        add_custom_target(
            format-check
            COMMAND "${CORAX_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${corax_cpp_files}
            COMMENT "Checking Corax C++ formatting"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    else()
        add_custom_target(
            format
            COMMAND "${CMAKE_COMMAND}" -E echo "clang-format is required for the format target"
            COMMAND "${CMAKE_COMMAND}" -E false
        )
        add_custom_target(
            format-check
            COMMAND "${CMAKE_COMMAND}" -E echo "clang-format is required for format-check"
            COMMAND "${CMAKE_COMMAND}" -E false
        )
    endif()

    if(TARGET Qt6::qmlformat)
        add_custom_target(
            qml-format
            COMMAND
                "${CMAKE_COMMAND}"
                "-DQMLFORMAT_EXECUTABLE=$<TARGET_FILE:Qt6::qmlformat>"
                "-DQML_ROOT=${CMAKE_SOURCE_DIR}/src/ui"
                -DMODE=format
                -P "${CMAKE_SOURCE_DIR}/cmake/QmlFormat.cmake"
            COMMENT "Formatting Corax QML sources"
            VERBATIM
        )
        add_custom_target(
            qml-format-check
            COMMAND
                "${CMAKE_COMMAND}"
                "-DQMLFORMAT_EXECUTABLE=$<TARGET_FILE:Qt6::qmlformat>"
                "-DQML_ROOT=${CMAKE_SOURCE_DIR}/src/ui"
                -DMODE=check
                -P "${CMAKE_SOURCE_DIR}/cmake/QmlFormat.cmake"
            COMMENT "Checking Corax QML formatting"
            VERBATIM
        )
    endif()

    if(TARGET all_qmllint)
        add_custom_target(qml-lint DEPENDS all_qmllint)
    endif()

    find_program(CORAX_CLANG_TIDY_EXECUTABLE NAMES clang-tidy-18 clang-tidy)
    if(CORAX_CLANG_TIDY_EXECUTABLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        file(
            GLOB_RECURSE corax_analysis_files
            CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/*.cpp"
        )
        add_custom_target(
            analyze
            COMMAND
                "${CORAX_CLANG_TIDY_EXECUTABLE}"
                -p "${CMAKE_BINARY_DIR}"
                --quiet
                ${corax_analysis_files}
            COMMENT "Running clang-tidy on Corax production sources"
            DEPENDS corax_app
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    elseif(CORAX_CLANG_TIDY_EXECUTABLE)
        add_custom_target(
            analyze
            COMMAND
                "${CMAKE_COMMAND}" -E echo
                "The analyze target requires the CMake analysis preset."
            COMMAND "${CMAKE_COMMAND}" -E false
            VERBATIM
        )
    else()
        add_custom_target(
            analyze
            COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy is required for the analyze target"
            COMMAND "${CMAKE_COMMAND}" -E false
        )
    endif()

    add_custom_target(
        architecture-check
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckBoundaries.cmake"
        COMMENT "Checking source-level architecture boundaries"
        VERBATIM
    )

    if(BUILD_TESTING)
        add_test(
            NAME architecture_boundaries
            COMMAND
                "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
                -P "${CMAKE_SOURCE_DIR}/cmake/CheckBoundaries.cmake"
        )
    endif()
endfunction()
