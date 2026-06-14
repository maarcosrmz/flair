# Copyright (c) 2024-2026   Flatiron Institute
# Copyright (c) 2026        Marcos Ramirez Joos
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# This file has been modified from its original version.

# ========= Additional Depdencies ==========

# LLVM / Clang
message(STATUS "-------- LLVM / Clang detection -------------")
include(clang_detection)

# LLVM / Flang
message(STATUS "-------- LLVM / Flang detection -------------")
include(flang_detection)

# fmt
if(NOT TARGET fmt)
  message(STATUS "-------- Fetch & Configure FMT -------------")
  include(FetchContent)
  FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        main
  EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(fmt)
  set_property(TARGET fmt PROPERTY POSITION_INDEPENDENT_CODE ON)
else()
  message(STATUS "-------- Reusing existing fmt target -------------")
endif()

# ========= Dynamic Analyzer Checks ==========

option(ASAN "Compile library and executables with LLVM Address Sanitizer" OFF)
if(ASAN)
  if(NOT TARGET asan)
    find_package(sanitizer REQUIRED COMPONENTS asan)
  endif()
endif()

option(UBSAN "Compile library and executables with LLVM Undefined Behavior Sanitizer" OFF)
if(UBSAN)
  if(NOT TARGET ubsan)
    find_package(sanitizer REQUIRED COMPONENTS ubsan)
  endif()
endif()

option(MSAN "Compile library and executables with LLVM Memory Sanitizer" OFF)
if(MSAN)
  if(NOT TARGET msan)
    find_package(sanitizer REQUIRED COMPONENTS msan)
  endif()
endif()

option(TSAN "Compile library and executables with LLVM Thread Sanitizer" OFF)
if(TSAN)
  if(NOT TARGET tsan)
    find_package(sanitizer REQUIRED COMPONENTS tsan)
  endif()
endif()

# ========= Build all components ==========

add_subdirectory(flu)
add_subdirectory(tools)
