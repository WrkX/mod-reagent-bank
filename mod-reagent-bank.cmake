# Optional CMake hooks for mod-reagent-bank.
#
# Source files under src/ are discovered automatically, conf/*.conf.dist files
# are copied automatically, and Addmod_reagent_bankScripts() is invoked by the
# generated module loader. This file only adds the optional headless tests.
#
# Included twice:
#   TORTOISE_MODULE_CMAKE_PHASE=DISCOVERY
#   TORTOISE_MODULE_CMAKE_PHASE=POST_TARGETS
#
# Use TW_* helpers. Do not use AzerothCore AC_* names.

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "DISCOVERY")
  # Default discovery is enough for src/ and conf/.
endif()

if (BUILD_TESTING AND TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
    function(define_reagent_bank_tests)
        set(MOD_PATH "${CMAKE_SOURCE_DIR}/modules/mod-reagent-bank")

        add_executable(reagent_bank_tests
            "${MOD_PATH}/t/TestReagentBankProtocol.cpp"
            "${MOD_PATH}/t/TestReagentBankRules.cpp"
            "${MOD_PATH}/src/ReagentBankConfig.cpp"
            "${MOD_PATH}/src/ReagentBankProtocol.cpp"
            "${MOD_PATH}/src/ReagentBankStore.cpp"
        )

        target_compile_definitions(reagent_bank_tests PRIVATE
            REAGENT_BANK_HEADLESS_TESTS=1
            REAGENT_BANK_SQL_PATH="${MOD_PATH}/data/sql/character/20260907000000_reagent_bank.sql"
        )

        target_link_libraries(reagent_bank_tests
            gtest_main
        )

        target_include_directories(reagent_bank_tests PRIVATE
            "${MOD_PATH}/src"
        )

        set_target_properties(reagent_bank_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endfunction()

    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL define_reagent_bank_tests)
endif()
