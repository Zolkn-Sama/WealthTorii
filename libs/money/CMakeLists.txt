add_library(wealthtorii_money
        src/money.cpp
        src/currency.cpp
)

add_library(wealthtorii::money ALIAS wealthtorii_money)

target_include_directories(wealthtorii_money
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_money PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_money
        PRIVATE
        wealthtorii::warnings
        PUBLIC
        wealthtorii::sanitizers
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_money_tests
            tests/money_tests.cpp
            tests/currency_tests.cpp
    )

    target_link_libraries(wealthtorii_money_tests
            PRIVATE
            wealthtorii::money
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_money_tests)
endif()
