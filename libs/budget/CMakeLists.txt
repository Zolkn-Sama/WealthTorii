add_library(wealthtorii_budget
        src/category.cpp
        src/budget.cpp
        src/allocation.cpp
)

add_library(wealthtorii::budget ALIAS wealthtorii_budget)

target_include_directories(wealthtorii_budget
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_budget PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_budget
        PUBLIC
        wealthtorii::money
        wealthtorii::ledger
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_budget_tests
            tests/category_tests.cpp
            tests/budget_tests.cpp
            tests/allocation_tests.cpp
    )

    target_link_libraries(wealthtorii_budget_tests
            PRIVATE
            wealthtorii::budget
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_budget_tests)
endif()
