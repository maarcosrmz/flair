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

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
cmake_policy(VERSION ${CMAKE_VERSION})

# ############
# Define Project
project(flair VERSION 0.1.0 LANGUAGES C CXX)
get_directory_property(IS_SUBPROJECT PARENT_DIRECTORY)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "Flair must be compiled with Clang")
endif()

message(STATUS "-------- flair version and git hash detection -------------")
find_package(Git)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_VARIABLE PROJECT_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "flair version : ${PROJECT_VERSION}")
message(STATUS "flair Git hash: ${PROJECT_GIT_HASH}")

# Assert that install directory is provided and is absolute path.
if(NOT IS_SUBPROJECT)
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR (NOT IS_ABSOLUTE ${CMAKE_INSTALL_PREFIX}))
    message(FATAL_ERROR "CMAKE_INSTALL_PREFIX must be specified and must be an absolute path.\n There is no default.\n Current value : ${CMAKE_INSTALL_PREFIX}\n.  e.g. ... -DCMAKE_INSTALL_PREFIX=$HOME/flair_install")
  endif()
  message(STATUS "-------- CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX} --------")
endif()
set(flair_BINARY_DIR ${PROJECT_BINARY_DIR} CACHE STRING "Binary directory of the flair Project")

# Make additional Find Modules available
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/share/cmake/Modules)

# ############
# CMake Options

# Default to Release build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Type of build" FORCE)
endif()
if(NOT IS_SUBPROJECT)
  message(STATUS "-------- BUILD-TYPE: ${CMAKE_BUILD_TYPE} --------")
endif()

# ############
# Global Compilation Settings

# Build static libraries by default
option(BUILD_SHARED_LIBS "Enable compilation of shared libraries" OFF)

# Export the list of compile-commands into compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Disable compiler extensions
set(CMAKE_CXX_EXTENSIONS OFF)

# Provide additional debugging information for Debug builds
add_compile_options($<$<CONFIG:Debug>:-ggdb3>)

# Create an Interface target for compiler warnings
add_library(flair_warnings INTERFACE)
target_compile_options(flair_warnings
  INTERFACE
    -Wall
    -Wextra
    -Wfloat-conversion
    -Wpedantic
    -Wno-sign-compare
    -Wno-deprecated-comma-subscript
    -Wno-unknown-warning-option
    -Wshadow
    -Wno-gcc-compat
    -Wno-c++17-extensions
    -Wno-c++17-compat
)

# ---------------------------------
# Resolve Clang Linktime Problems
# CMake will adjust any linker flags from '-L path_to/mylib.so' to -lmylib
# if the proper mylib.so is automatically found by the linker, i.e.
# the directory comes first in LIBRARY_PATH.
# The clang linker however ignores LIBRARY_PATH.
# We thus explicitly add the content of LIBRARY_PATH to the LDFLAGS
# FIXME For future cmake versions we should populate the
# INTERFACE_LINK_DIRECTORIES of the target
# ---------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND DEFINED ENV{LIBRARY_PATH})
  string(REPLACE ":" ";" LINK_DIRS $ENV{LIBRARY_PATH})
  foreach(dir ${LINK_DIRS})
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -L${dir}")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS " -L${dir}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -L${dir}")
  endforeach()
endif()

# Provide GNU Installation directories
include(GNUInstallDirs)

# Python
message(STATUS "-------- Python detection -------------")
find_package(Python COMPONENTS Interpreter Development NumPy)
string(REGEX REPLACE "/.*/lib" "lib" PYTHON_LIB_DEST_ROOT ${Python_SITEARCH})
set(PYTHON_LIB_DEST_ROOT ${PYTHON_LIB_DEST_ROOT} CACHE PATH "Python library destination")

# #############
# Build Project

# Build flair components
add_subdirectory(src)
