# SPDX-License-Identifier: Apache-2.0

add_executable(corax_presentation_tests ProjectControllerTests.cpp)

target_link_libraries(
    corax_presentation_tests
    PRIVATE
        Corax::Application
        Corax::Presentation
        Qt6::Core
        Qt6::Test
)

target_compile_features(corax_presentation_tests PRIVATE cxx_std_23)
set_target_properties(corax_presentation_tests PROPERTIES CXX_EXTENSIONS OFF AUTOMOC ON)
corax_enable_warnings(corax_presentation_tests)

add_test(NAME corax.presentation COMMAND corax_presentation_tests)
