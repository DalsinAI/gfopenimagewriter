# GFOpenImageWriter's host build: the tests of the plain-C parts (the Amiga program is build-amiga.sh).
# MIT, Copyright (c) 2026 AmiCradle Team.
cmake_minimum_required(VERSION 3.16)
project(gfopenimagewriter C)
set(CMAKE_C_STANDARD 99)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror)
endif()
enable_testing()
add_executable(test_iw_drive tests/test_iw_drive.c iw_drive.c)
target_include_directories(test_iw_drive PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME iw_drive COMMAND test_iw_drive)
