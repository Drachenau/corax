qt_add_executable(corax_app
    WIN32
    main.cpp
)

target_link_libraries(
    corax_app
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
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

target_compile_features(corax_app PRIVATE cxx_std_23)
set_target_properties(
    corax_app
    PROPERTIES
        CXX_EXTENSIONS OFF
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${CORAX_APPLICATION_IDENTIFIER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        OUTPUT_NAME corax
)

corax_enable_warnings(corax_app)
qt_import_qml_plugins(corax_app)

if(BUILD_TESTING)
    add_test(NAME corax.app.smoke COMMAND $<TARGET_FILE:corax_app>)
    set_tests_properties(
        corax.app.smoke
        PROPERTIES
            ENVIRONMENT
                "CORAX_SMOKE_TEST=1;QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
    )

    add_test(NAME corax.app.qml_root_failure COMMAND $<TARGET_FILE:corax_app>)
    set_tests_properties(
        corax.app.qml_root_failure
        PROPERTIES
            ENVIRONMENT
                "CORAX_SMOKE_TEST=1;CORAX_SMOKE_TEST_FORCE_QML_FAILURE=1;QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
            WILL_FAIL TRUE
    )
endif()

install(TARGETS corax_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
