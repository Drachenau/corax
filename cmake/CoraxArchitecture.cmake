function(corax_assert_corax_dependencies target)
    cmake_parse_arguments(ARG "" "" "ALLOWED;ALLOWED_QT;QT_GENERATED" ${ARGN})

    get_target_property(link_libraries ${target} LINK_LIBRARIES)
    if(NOT link_libraries)
        return()
    endif()

    set(forbidden_dependencies)
    foreach(link_library IN LISTS link_libraries)
        set(candidate "${link_library}")
        if(TARGET "${link_library}")
            get_target_property(aliased_target "${link_library}" ALIASED_TARGET)
            if(aliased_target)
                set(candidate "${aliased_target}")
            endif()
        endif()

        if(candidate MATCHES "^corax_[A-Za-z0-9_]+$")
            list(FIND ARG_ALLOWED "${candidate}" allowed_index)
            if(allowed_index EQUAL -1)
                string(REPLACE ";" ", " allowed_corax "${ARG_ALLOWED}")
                list(
                    APPEND forbidden_dependencies
                    "${candidate} (allowed Corax dependencies: ${allowed_corax})"
                )
            endif()
        elseif(candidate MATCHES "^Qt6::([A-Za-z0-9_]+)$")
            set(qt_component "${CMAKE_MATCH_1}")
            list(FIND ARG_ALLOWED_QT "${qt_component}" allowed_qt_index)
            list(FIND ARG_QT_GENERATED "${qt_component}" generated_qt_index)
            if(allowed_qt_index EQUAL -1 AND generated_qt_index EQUAL -1)
                string(REPLACE ";" ", " allowed_qt "${ARG_ALLOWED_QT}")
                string(REPLACE ";" ", " generated_qt "${ARG_QT_GENERATED}")
                string(
                    CONCAT dependency_description
                    "${candidate} (allowed Qt components: ${allowed_qt}, "
                    "Qt-generated components: ${generated_qt})"
                )
                list(APPEND forbidden_dependencies "${dependency_description}")
            endif()
        endif()
    endforeach()

    if(forbidden_dependencies)
        list(JOIN forbidden_dependencies "\n  - " dependency_report)
        message(
            FATAL_ERROR
            "${target} has forbidden direct dependencies:\n  - ${dependency_report}"
        )
    endif()
endfunction()
