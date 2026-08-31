qt_add_executable(corax_app
    main.cpp
)

target_link_libraries(
    corax_app
    PRIVATE
        Corax::Application
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
        OUTPUT_NAME corax
)

corax_enable_warnings(corax_app)
qt_import_qml_plugins(corax_app)

install(TARGETS corax_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
