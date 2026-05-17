add_library(wealthtorii_market_data
        src/stooq.cpp
)

add_library(wealthtorii::market_data ALIAS wealthtorii_market_data)

target_include_directories(wealthtorii_market_data
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_market_data PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_market_data
        PUBLIC
        wealthtorii::money
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_market_data_tests
            tests/stooq_tests.cpp
    )

    target_link_libraries(wealthtorii_market_data_tests
            PRIVATE
            wealthtorii::market_data
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_market_data_tests)
endif()
