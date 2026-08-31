# SPDX-License-Identifier: Apache-2.0

add_library(corax_storage_sqlite STATIC)

target_sources(corax_storage_sqlite
    PRIVATE
        src/ProjectManifest.cpp
        src/ProjectWriterLock.cpp
        src/SqliteProjectDatabase.cpp
        src/SqliteProjectStore.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/corax/storage_sqlite/ProjectManifest.h
            include/corax/storage_sqlite/ProjectWriterLock.h
            include/corax/storage_sqlite/SqliteProjectDatabase.h
            include/corax/storage_sqlite/SqliteProjectStore.h
)

target_link_libraries(corax_storage_sqlite
    PUBLIC
        corax_application
        corax_domain
        Qt6::Core
    PRIVATE
        corax_sqlite
)

target_compile_features(corax_storage_sqlite PUBLIC cxx_std_23)
set_target_properties(corax_storage_sqlite PROPERTIES CXX_EXTENSIONS OFF)
corax_enable_warnings(corax_storage_sqlite)

add_library(Corax::StorageSqlite ALIAS corax_storage_sqlite)
