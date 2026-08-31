qt_add_executable(corax_app_integration_tests test_app_integration.cpp)

target_link_libraries(
    corax_app_integration_tests
    PRIVATE
        Corax::Application
        Corax::BuildConfig
        corax_jobs
        corax_platform
        Corax::Presentation
        Corax::StorageSqlite
        Corax::Ui
        corax_uiplugin
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::Test
)

target_compile_features(corax_app_integration_tests PRIVATE cxx_std_23)
set_target_properties(corax_app_integration_tests PROPERTIES CXX_EXTENSIONS OFF)
corax_enable_warnings(corax_app_integration_tests)

qt_import_qml_plugins(corax_app_integration_tests)

add_test(NAME corax.app.integration COMMAND corax_app_integration_tests)
set_tests_properties(
    corax.app.integration
    PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
)
