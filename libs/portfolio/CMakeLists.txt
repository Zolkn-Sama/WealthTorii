add_library(wealthtorii_portfolio
        src/portfolio.cpp
)

add_library(wealthtorii::portfolio ALIAS wealthtorii_portfolio)

target_include_directories(wealthtorii_portfolio
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_portfolio PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_portfolio
        PUBLIC
        wealthtorii::money
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_portfolio_tests
            tests/portfolio_tests.cpp
    )

    target_link_libraries(wealthtorii_portfolio_tests
            PRIVATE
            wealthtorii::portfolio
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_portfolio_tests)
endif()
