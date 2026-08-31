# SPDX-License-Identifier: Apache-2.0

add_library(corax_application STATIC)

target_sources(corax_application
    PRIVATE
        src/ProjectService.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/corax/application/IProjectStore.h
            include/corax/application/ProjectService.h
)

target_link_libraries(corax_application PUBLIC corax_domain Qt6::Core)

target_compile_features(corax_application PUBLIC cxx_std_23)
set_target_properties(corax_application PROPERTIES CXX_EXTENSIONS OFF)
corax_enable_warnings(corax_application)

add_library(Corax::Application ALIAS corax_application)
