set(corax_sqlite3_c_sha3_256
    "67f423e9ebbbdc473cbc4772c872ee6b89f31fde4ed0279a5c25d5f65c043a16"
)
set(corax_sqlite3_h_sha3_256
    "89d0de498b1012938bb78dc18b68fea52e26f7f7b6db6347d126a2e58b86a05c"
)
file(SHA3_256 "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c" corax_sqlite3_c_actual)
file(SHA3_256 "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h" corax_sqlite3_h_actual)
if(NOT corax_sqlite3_c_actual STREQUAL corax_sqlite3_c_sha3_256
   OR NOT corax_sqlite3_h_actual STREQUAL corax_sqlite3_h_sha3_256)
    message(FATAL_ERROR "Vendored SQLite sources do not match dependencies.lock.json")
endif()

add_library(corax_sqlite STATIC sqlite3.c)
add_library(Corax::SQLite ALIAS corax_sqlite)

set_target_properties(
    corax_sqlite
    PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        FOLDER "ThirdParty"
)

target_include_directories(corax_sqlite INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(
    corax_sqlite
    PRIVATE
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_DQS=0
        SQLITE_ENABLE_FTS5
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_THREADSAFE=1
)
target_link_libraries(corax_sqlite PRIVATE Threads::Threads)

if(UNIX)
    target_link_libraries(corax_sqlite PRIVATE m)
endif()

if(MSVC)
    target_compile_options(corax_sqlite PRIVATE /w)
else()
    target_compile_options(corax_sqlite PRIVATE -w)
endif()
