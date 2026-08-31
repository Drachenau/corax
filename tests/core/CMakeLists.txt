# SPDX-License-Identifier: Apache-2.0

add_executable(corax_core_tests CoreTests.cpp)

target_link_libraries(corax_core_tests
    PRIVATE
        Corax::Application
        Corax::Domain
        Qt6::Core
        Qt6::Test
)

target_compile_features(corax_core_tests PRIVATE cxx_std_23)
set_target_properties(corax_core_tests PROPERTIES CXX_EXTENSIONS OFF AUTOMOC ON)
corax_enable_warnings(corax_core_tests)

add_test(NAME corax.core COMMAND corax_core_tests)
