add_library(corax_jobs STATIC)

target_sources(
    corax_jobs
    PRIVATE
        src/CancellationToken.cpp
        src/FakeWorkUnitJob.cpp
        src/Job.cpp
        src/JobScheduler.cpp
        src/JobTypes.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/corax/jobs/CancellationToken.h
            include/corax/jobs/FakeWorkUnitJob.h
            include/corax/jobs/Job.h
            include/corax/jobs/JobScheduler.h
            include/corax/jobs/JobTypes.h
)

target_link_libraries(corax_jobs PUBLIC Qt6::Core)
target_compile_features(corax_jobs PUBLIC cxx_std_23)
set_target_properties(corax_jobs PROPERTIES CXX_EXTENSIONS OFF)

corax_enable_warnings(corax_jobs)
