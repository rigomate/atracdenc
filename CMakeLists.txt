CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

if(RUN_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=clang-analyzer*;-export-fixes=clang-fixes.yaml")
endif()

project(atracdenc)

if (UNIX)
    add_subdirectory(3rd/gtest-1.7.0)
    add_subdirectory(test)
endif()
add_subdirectory(src)
