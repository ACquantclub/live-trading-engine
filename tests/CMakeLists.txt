# Google Test setup
include(GoogleTest)

# Create test executable for unit tests
file(GLOB_RECURSE UNIT_TEST_SOURCES "unit/*.cpp")
if(UNIT_TEST_SOURCES)
    add_executable(unit_tests ${UNIT_TEST_SOURCES})
    target_link_libraries(unit_tests 
        PRIVATE 
        trading_engine
        gtest 
        gtest_main 
        gmock
        gmock_main
        Threads::Threads
        csp_core
        csp_types
        csp_engine
    )
    
    # Discover tests
    gtest_discover_tests(unit_tests)
endif()

# Create test executable for integration tests
file(GLOB_RECURSE INTEGRATION_TEST_SOURCES "integration/*.cpp")
if(INTEGRATION_TEST_SOURCES)
    add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
    target_link_libraries(integration_tests 
        PRIVATE 
        trading_engine
        gtest 
        gtest_main 
        gmock
        gmock_main
        Threads::Threads
    )
    
    # Discover tests
    gtest_discover_tests(integration_tests)
endif() 