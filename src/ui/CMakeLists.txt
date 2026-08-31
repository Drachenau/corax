set_source_files_properties(
    qml/design/DesignTokens.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_library(corax_ui STATIC)

qt_add_qml_module(
    corax_ui
    URI Corax.Ui
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Corax/Ui"
    RESOURCE_PREFIX /qt/qml
    SOURCES
        QmlTypes.h
    QML_FILES
        qml/Main.qml
        qml/components/FocusButton.qml
        qml/design/DesignTokens.qml
)

target_sources(
    corax_ui
    PRIVATE
        src/UiConfiguration.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/corax/ui/UiConfiguration.h
)

target_link_libraries(
    corax_ui
    PUBLIC
        Corax::Presentation
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_compile_features(corax_ui PUBLIC cxx_std_23)
set_target_properties(corax_ui PROPERTIES CXX_EXTENSIONS OFF)

corax_enable_warnings(corax_ui)

add_library(Corax::Ui ALIAS corax_ui)
