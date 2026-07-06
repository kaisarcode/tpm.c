cmake_minimum_required(VERSION 3.14)
project(tpm C)

set(CMAKE_C_STANDARD 11)
if(NOT DEFINED KC_TPM_BUILD_VERSION)
    set(KC_TPM_BUILD_VERSION 0)
endif()

option(TPM_NATIVE "Enable native CPU optimizations" OFF)
option(TPM_BUILD_TESTS "Build tpm tests" OFF)

add_definitions(-DKC_TPM_BUILD_VERSION=${KC_TPM_BUILD_VERSION})

set(TPM_TEST_EXTERNAL_ARTIFACTS OFF)
if(TPM_BUILD_TESTS AND TPM_TEST_SHARED_LIBRARY)
    set(TPM_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(TPM_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(TPM_SOURCES src/libtpm.c)
set(TPM_HEADERS src/tpm.h)

set_source_files_properties(src/libtpm.c PROPERTIES
    COMPILE_DEFINITIONS KC_TPM_BUILD_VERSION=${KC_TPM_BUILD_VERSION}
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

    if(UNIX AND NOT ANDROID AND NOT APPLE)
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
    if(TPM_TEST_CLI)
        target_compile_definitions(tpm_contract_test PRIVATE
            KC_TPM_TEST_CLI="${TPM_TEST_CLI}"
        )
    endif()

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(tpm_contract_test PRIVATE m)
    elseif(ANDROID)
        target_link_libraries(tpm_contract_test PRIVATE m)
    endif()

    add_custom_command(TARGET tpm_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${TPM_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:tpm_contract_test>
    )

    add_test(NAME library-kc_tpm_options_default COMMAND tpm_contract_test kc_tpm_options_default)
    add_test(NAME library-kc_tpm_options_load_env COMMAND tpm_contract_test kc_tpm_options_load_env)
    add_test(NAME library-kc_tpm_options_free COMMAND tpm_contract_test kc_tpm_options_free)
    add_test(NAME library-kc_tpm_on_signal COMMAND tpm_contract_test kc_tpm_on_signal)
    add_test(NAME library-kc_tpm_raise_signal COMMAND tpm_contract_test kc_tpm_raise_signal)
    add_test(NAME library-kc_tpm_stop COMMAND tpm_contract_test kc_tpm_stop)
    add_test(NAME library-kc_tpm_stop_requested COMMAND tpm_contract_test kc_tpm_stop_requested)
    add_test(NAME library-kc_tpm_listen_signals COMMAND tpm_contract_test kc_tpm_listen_signals)
    add_test(NAME library-kc_tpm_listen_signal COMMAND tpm_contract_test kc_tpm_listen_signal)
    add_test(NAME library-kc_tpm_signal_listener COMMAND tpm_contract_test kc_tpm_signal_listener)
    add_test(NAME library-kc_tpm_open COMMAND tpm_contract_test kc_tpm_open)
    add_test(NAME library-kc_tpm_build COMMAND tpm_contract_test kc_tpm_build)
    add_test(NAME library-kc_tpm_score COMMAND tpm_contract_test kc_tpm_score)
    add_test(NAME library-kc_tpm_close COMMAND tpm_contract_test kc_tpm_close)
    add_test(NAME library-kc_tpm_version COMMAND tpm_contract_test kc_tpm_version)
    add_test(NAME library-ctrl-help COMMAND tpm_contract_test ctrl-help)
    add_test(NAME library-ctrl-stop COMMAND tpm_contract_test ctrl-stop)
    add_test(NAME library-ctrl-get COMMAND tpm_contract_test ctrl-get)
    add_test(NAME library-ctrl-set COMMAND tpm_contract_test ctrl-set)
endif()
