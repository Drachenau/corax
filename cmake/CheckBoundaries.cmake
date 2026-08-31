if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(
    GLOB_RECURSE production_files
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.cc"
    "${SOURCE_ROOT}/src/*.cxx"
    "${SOURCE_ROOT}/src/*.ixx"
    "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/src/*.hh"
    "${SOURCE_ROOT}/src/*.hpp"
    "${SOURCE_ROOT}/src/*.hxx"
)

string(ASCII 9 horizontal_tab)
set(horizontal_whitespace " ${horizontal_tab}")
set(
    qt_private_header_pattern
    "#[${horizontal_whitespace}]*INCLUDE[${horizontal_whitespace}]*[<\"](QT[A-Z0-9_]+/([^>\"]*/)?)?PRIVATE/[^>\"]+_P\\.H[>\"]"
)

foreach(source_file IN LISTS production_files)
    file(READ "${source_file}" source_content)
    string(TOUPPER "${source_content}" normalized_source_content)
    string(REGEX REPLACE "#[ \\t]*PRAGMA[ \\t]+ONCE" "" normalized_source_content
                         "${normalized_source_content}")
    if(normalized_source_content MATCHES "${qt_private_header_pattern}")
        message(FATAL_ERROR "Qt private API header used by Corax source: ${source_file}")
    endif()

    if(source_file MATCHES "/src/storage_sqlite/")
        continue()
    endif()

    if(normalized_source_content MATCHES "SQLITE3\\.H|SQLITE3_[A-Z0-9_]+")
        message(FATAL_ERROR "SQLite API escaped corax_storage_sqlite: ${source_file}")
    endif()
    if(normalized_source_content MATCHES "(SELECT|INSERT|UPDATE|DELETE|CREATE TABLE|PRAGMA)[ \\n\\t]+[A-Z_(]")
        message(FATAL_ERROR "SQL text escaped corax_storage_sqlite: ${source_file}")
    endif()
endforeach()

file(
    GLOB_RECURSE qml_files
    "${SOURCE_ROOT}/src/ui/*.qml"
    "${SOURCE_ROOT}/src/ui/*.js"
    "${SOURCE_ROOT}/src/ui/*.mjs"
)
foreach(qml_file IN LISTS qml_files)
    file(READ "${qml_file}" qml_content)
    if(qml_content MATCHES "XMLHttpRequest|LocalStorage|Qt\\.labs\\.settings|JSON\\.(parse|stringify)|FolderListModel|executeSql")
        message(FATAL_ERROR "QML contains a forbidden implementation API: ${qml_file}")
    endif()
endforeach()
