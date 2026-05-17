find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

add_executable(wt_api
        src/main.cpp
)

target_compile_features(wt_api PRIVATE cxx_std_20)

# Built React front (web/dist) is served statically by the API when present.
target_compile_definitions(wt_api
        PRIVATE
        WEALTHTORII_WEB_ROOT="${CMAKE_SOURCE_DIR}/web/dist"
)

# wealthtorii_cli_lib re-exports the budget/rules config helpers and, transitively,
# every domain library the API needs (money, ledger, budget, import, analytics, storage).
target_link_libraries(wt_api
        PRIVATE
        wealthtorii_cli_lib
        wealthtorii::portfolio
        wealthtorii::market_data
        Drogon::Drogon
        unofficial-sodium::sodium
        jwt-cpp::jwt-cpp
        wealthtorii::warnings
)
