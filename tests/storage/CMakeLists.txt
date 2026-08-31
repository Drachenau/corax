# SPDX-License-Identifier: Apache-2.0

add_executable(corax_storage_tests StorageTests.cpp)

target_link_libraries(corax_storage_tests
    PRIVATE
        Corax::Application
        Corax::Domain
        Corax::StorageSqlite
        Qt6::Core
        Qt6::Test
)

target_compile_features(corax_storage_tests PRIVATE cxx_std_23)
set_target_properties(corax_storage_tests PROPERTIES CXX_EXTENSIONS OFF AUTOMOC ON)
corax_enable_warnings(corax_storage_tests)

add_test(NAME corax.storage COMMAND corax_storage_tests)
