# SPDX-License-Identifier: Apache-2.0

add_library(corax_domain STATIC)

target_sources(corax_domain
    PRIVATE
        src/AppError.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/corax/domain/AppError.h
            include/corax/domain/ProjectInfo.h
            include/corax/domain/Result.h
)

target_link_libraries(corax_domain PUBLIC Qt6::Core)

target_compile_features(corax_domain PUBLIC cxx_std_23)
set_target_properties(corax_domain PROPERTIES CXX_EXTENSIONS OFF)
corax_enable_warnings(corax_domain)

add_library(Corax::Domain ALIAS corax_domain)
