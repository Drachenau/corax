cmake_minimum_required(VERSION 3.28)

project(
    Corax
    VERSION 0.1.0
    DESCRIPTION "A native workspace for curating image-training datasets"
    LANGUAGES C CXX
)

include(CTest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(CORAX_WARNINGS_AS_ERRORS "Treat warnings in Corax-owned code as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CoraxArchitecture)
include(CoraxDeveloperTools)
include(CoraxWarnings)

find_package(Threads REQUIRED)
set(corax_qt_components Core Gui Qml QmlTools Quick QuickControls2)
if(BUILD_TESTING)
    list(APPEND corax_qt_components Test)
endif()
find_package(Qt6 6.8.3 REQUIRED COMPONENTS ${corax_qt_components})

qt_standard_project_setup(REQUIRES 6.8)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

add_subdirectory(third_party/sqlite)
add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/storage_sqlite)
add_subdirectory(src/jobs)
add_subdirectory(src/presentation)
add_subdirectory(src/ui)
add_subdirectory(src/platform)
add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

corax_assert_corax_dependencies(corax_domain ALLOWED_QT Core)
corax_assert_corax_dependencies(
    corax_application
    ALLOWED corax_domain
    ALLOWED_QT Core
)
corax_assert_corax_dependencies(
    corax_storage_sqlite
    ALLOWED corax_application corax_domain corax_sqlite
    ALLOWED_QT Core
)
corax_assert_corax_dependencies(
    corax_jobs
    ALLOWED corax_application corax_domain
    ALLOWED_QT Core
)
corax_assert_corax_dependencies(
    corax_presentation
    ALLOWED corax_application corax_domain corax_jobs
    ALLOWED_QT Core
)
corax_assert_corax_dependencies(
    corax_ui
    ALLOWED corax_presentation
    ALLOWED_QT Core Qml QmlIntegration Quick QuickControls2
    # Qt 6.8.3 adds this implementation target from qt_add_qml_module. Corax
    # sources must still pass the private-header check in CheckBoundaries.cmake.
    QT_GENERATED QmlPrivate
)
corax_assert_corax_dependencies(
    corax_platform
    ALLOWED corax_application corax_domain
    ALLOWED_QT Core
)
corax_assert_corax_dependencies(
    corax_app
    ALLOWED
        corax_application
        corax_jobs
        corax_platform
        corax_presentation
        corax_storage_sqlite
        corax_ui
        corax_uiplugin
    ALLOWED_QT Core Gui Qml Quick
)

corax_add_developer_tools()
