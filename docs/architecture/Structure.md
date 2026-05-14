
## Structure du projet

```txt
WealthTorii/

  cmake/
    ProjectOptions.cmake
    Warnings.cmake
    Sanitizers.cmake

  apps/
    api/
    cli/

  libs/
    money/
        include/
            wealthtorii/
                money/
                    currency.hpp
                    money.hpp
        src/
            currency.cpp
            money.cpp
        tests/
            currency_tests.cpp
            money_tests.cpp
    ledger/
        include/
            wealthtorii/
                ledger/
                    account.hpp
        src/
            account.cpp
        tests/
            account_tests.cpp
    portfolio/
    market_data/
    analytics/

  tests/
    integration/
    e2e/

  docs/
    adr/
    architecture/
        architechure.md

  scripts/

  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  .clang-format
  .clang-tidy