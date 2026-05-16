find_package(Drogon CONFIG REQUIRED)

add_executable(wt_api
        src/main.cpp
)

target_compile_features(wt_api PRIVATE cxx_std_20)

# wealthtorii_cli_lib re-exports the budget/rules config helpers and, transitively,
# every domain library the API needs (money, ledger, budget, import, analytics, storage).
target_link_libraries(wt_api
        PRIVATE
        wealthtorii_cli_lib
        Drogon::Drogon
        wealthtorii::warnings
)
