function(corax_assert_corax_dependencies target)
    cmake_parse_arguments(ARG "" "" "ALLOWED;ALLOWED_QT;QT_GENERATED" ${ARGN})

    get_target_property(link_libraries ${target} LINK_LIBRARIES)
    if(NOT link_libraries)
        return()
    endif()

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
                message(
                    FATAL_ERROR
                    "${target} has forbidden direct Corax dependency ${candidate}. "
                    "Allowed Corax dependencies: ${ARG_ALLOWED}"
                )
            endif()
        elseif(candidate MATCHES "^Qt6::([A-Za-z0-9_]+)$")
            set(qt_component "${CMAKE_MATCH_1}")
            list(FIND ARG_ALLOWED_QT "${qt_component}" allowed_qt_index)
            list(FIND ARG_QT_GENERATED "${qt_component}" generated_qt_index)
            if(allowed_qt_index EQUAL -1 AND generated_qt_index EQUAL -1)
                message(
                    FATAL_ERROR
                    "${target} has forbidden direct Qt dependency ${candidate}. "
                    "Allowed Qt components: ${ARG_ALLOWED_QT}. "
                    "Qt-generated components: ${ARG_QT_GENERATED}"
                )
            endif()
        endif()
    endforeach()
endfunction()
