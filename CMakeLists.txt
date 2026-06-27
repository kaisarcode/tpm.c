cmake_minimum_required(VERSION 3.14)
project(tpm C)

set(CMAKE_C_STANDARD 11)
set(KC_TPM_BUILD_VERSION "0" CACHE STRING "tpm build version timestamp")


option(TPM_NATIVE "Enable native CPU optimizations" OFF)
option(TPM_BUILD_TESTS "Build tpm tests" OFF)

set(TPM_TEST_EXTERNAL_ARTIFACTS OFF)
if(TPM_BUILD_TESTS AND TPM_TEST_SHARED_LIBRARY)
    set(TPM_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(TPM_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(TPM_SOURCES src/libtpm.c)
set(TPM_HEADERS src/tpm.h)

set_property(
    SOURCE src/libtpm.c
    APPEND PROPERTY COMPILE_DEFINITIONS KC_TPM_BUILD_VERSION=${KC_TPM_BUILD_VERSION}
)

if(NOT TPM_TEST_EXTERNAL_ARTIFACTS)
    add_library(tpm_shared SHARED ${TPM_SOURCES})
    set_target_properties(tpm_shared PROPERTIES OUTPUT_NAME tpm PREFIX "lib")
    set_target_properties(tpm_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(tpm_shared PRIVATE src)

    add_library(tpm_static STATIC ${TPM_SOURCES})
    set_target_properties(tpm_static PROPERTIES
        OUTPUT_NAME tpm
        PREFIX "lib"
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(tpm_static PRIVATE src)

    add_executable(tpm_exe src/tpm.c ${TPM_SOURCES})
    set_target_properties(tpm_exe PROPERTIES OUTPUT_NAME tpm)
    target_include_directories(tpm_exe PRIVATE src)

    if(UNIX AND NOT ANDROID)
        target_link_libraries(tpm_shared m)
        target_link_libraries(tpm_static m)
        target_link_libraries(tpm_exe m)
    elseif(ANDROID)
        target_link_libraries(tpm_shared m)
        target_link_libraries(tpm_static m)
        target_link_libraries(tpm_exe m)
    endif()
endif()

if(TPM_BUILD_TESTS)
    enable_testing()

    if(TPM_TEST_SHARED_LIBRARY)
        add_library(tpm_test_shared SHARED IMPORTED)
        set_target_properties(tpm_test_shared PROPERTIES
            IMPORTED_LOCATION "${TPM_TEST_SHARED_LIBRARY}"
        )
        if(TPM_TEST_IMPORT_LIBRARY)
            set_target_properties(tpm_test_shared PROPERTIES
                IMPORTED_IMPLIB "${TPM_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(TPM_TEST_SHARED_TARGET tpm_shared)
    endif()

    if(TARGET tpm_test_shared)
        set(TPM_TEST_SHARED_TARGET tpm_test_shared)
    endif()

    add_executable(tpm_contract_test src/test.c)
    target_include_directories(tpm_contract_test PRIVATE src)
    target_link_libraries(tpm_contract_test PRIVATE ${TPM_TEST_SHARED_TARGET})

    if(UNIX AND NOT ANDROID)
        target_link_libraries(tpm_contract_test PRIVATE m)
    elseif(ANDROID)
        target_link_libraries(tpm_contract_test PRIVATE m)
    endif()

    add_custom_command(TARGET tpm_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${TPM_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:tpm_contract_test>
    )

    add_test(NAME library-version COMMAND tpm_contract_test version)
    add_test(NAME library-options COMMAND tpm_contract_test options)
    add_test(NAME library-open-close COMMAND tpm_contract_test open-close)
    add_test(NAME library-profile-score COMMAND tpm_contract_test profile-score)
    add_test(NAME library-stop COMMAND tpm_contract_test stop)
    add_test(NAME library-signals COMMAND tpm_contract_test signals)
    add_test(NAME library-multictx COMMAND tpm_contract_test multictx)

    if(TPM_TEST_CLI)
        add_executable(tpm_test_cli IMPORTED)
        set_target_properties(tpm_test_cli PROPERTIES
            IMPORTED_LOCATION "${TPM_TEST_CLI}"
        )
        set(TPM_TEST_CLI_COMMAND tpm_test_cli)
    else()
        set(TPM_TEST_CLI_COMMAND tpm_exe)
    endif()

    add_test(NAME cli-help-short COMMAND ${TPM_TEST_CLI_COMMAND} -h)
    add_test(NAME cli-help-long COMMAND ${TPM_TEST_CLI_COMMAND} --help)
    add_test(NAME cli-version-short COMMAND ${TPM_TEST_CLI_COMMAND} -v)
    add_test(NAME cli-version-long COMMAND ${TPM_TEST_CLI_COMMAND} --version)
    add_test(NAME cli-unknown-option COMMAND ${TPM_TEST_CLI_COMMAND} --bogus)
    add_test(NAME cli-missing-map COMMAND ${TPM_TEST_CLI_COMMAND})
    add_test(NAME cli-invalid-map COMMAND ${TPM_TEST_CLI_COMMAND} /nonexistent/map.file)
    add_test(NAME cli-missing-ngram-value COMMAND ${TPM_TEST_CLI_COMMAND} -n)
    set_tests_properties(
        cli-unknown-option
        cli-missing-map
        cli-invalid-map
        cli-missing-ngram-value
        PROPERTIES WILL_FAIL TRUE
    )
endif()
