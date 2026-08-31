add_library(corax_platform STATIC)

target_sources(
    corax_platform
    PRIVATE
        src/ApplicationIdentity.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/corax/platform/ApplicationIdentity.h
)
target_link_libraries(corax_platform PUBLIC Qt6::Core)
corax_enable_warnings(corax_platform)
