add_library(wealthtorii_analytics
        src/analytics.cpp
)

add_library(wealthtorii::analytics ALIAS wealthtorii_analytics)

target_include_directories(wealthtorii_analytics
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_analytics PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_analytics
        PUBLIC
        wealthtorii::ledger
        wealthtorii::money
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_analytics_tests
            tests/analytics_tests.cpp
    )

    target_link_libraries(wealthtorii_analytics_tests
            PRIVATE
            wealthtorii::analytics
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_analytics_tests)
endif()
