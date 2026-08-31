qt_add_executable(corax_qml_smoke_tests test_qml_smoke.cpp)

target_link_libraries(
    corax_qml_smoke_tests
    PRIVATE
        Corax::Application
        Corax::Presentation
        Corax::Ui
        corax_uiplugin
        Qt6::Quick
        Qt6::Qml
        Qt6::Test
)

target_compile_features(corax_qml_smoke_tests PRIVATE cxx_std_23)
set_target_properties(corax_qml_smoke_tests PROPERTIES CXX_EXTENSIONS OFF)
corax_enable_warnings(corax_qml_smoke_tests)

qt_import_qml_plugins(corax_qml_smoke_tests)

add_test(NAME corax_qml_smoke_tests COMMAND corax_qml_smoke_tests)
set_tests_properties(corax_qml_smoke_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
