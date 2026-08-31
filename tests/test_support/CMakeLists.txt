add_library(corax_test_support INTERFACE)

target_sources(
    corax_test_support
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/corax/test_support/DeterministicValues.h
)
target_link_libraries(corax_test_support INTERFACE Qt6::Core)
