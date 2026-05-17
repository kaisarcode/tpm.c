cmake_minimum_required(VERSION 3.14)
project(tpm C)

set(CMAKE_C_STANDARD 11)

option(TPM_NATIVE "Enable native CPU optimizations" OFF)

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

add_library(tpm_shared SHARED ${TPM_SOURCES})
set_target_properties(tpm_shared PROPERTIES OUTPUT_NAME tpm PREFIX "lib")
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
