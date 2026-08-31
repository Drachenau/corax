add_library(corax_presentation STATIC)

target_sources(
    corax_presentation
    PRIVATE
        src/JobListModel.cpp
        src/JobsController.cpp
        src/ProjectController.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/corax/presentation/JobListModel.h
            include/corax/presentation/JobsController.h
            include/corax/presentation/ProjectController.h
)

target_link_libraries(
    corax_presentation
    PUBLIC
        Corax::Application
        Qt6::Core
        corax_jobs
)

target_compile_features(corax_presentation PUBLIC cxx_std_23)
set_target_properties(corax_presentation PROPERTIES CXX_EXTENSIONS OFF)

corax_enable_warnings(corax_presentation)

# The UI module registers these QObject types through QML_FOREIGN wrappers.
# Export their metadata so qmllint and other QML tools can resolve the typed
# controller properties without adding a QML runtime dependency here.
qt6_extract_metatypes(corax_presentation)

add_library(Corax::Presentation ALIAS corax_presentation)
