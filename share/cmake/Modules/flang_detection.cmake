# Copyright (c) 2026 Marcos Ramirez Joos
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0

#=========================
# Locate Flang and LLVM
#=========================

find_package(MLIR REQUIRED CONFIG HINTS ${LLVM_ROOT} $ENV{LLVM_ROOT} ${MLIR_ROOT} $ENV{MLIR_ROOT})
find_package(Flang REQUIRED CONFIG HINTS ${LLVM_ROOT} $ENV{LLVM_ROOT} ${Flang_ROOT} $ENV{Flang_ROOT})
if(LLVM_VERSION VERSION_LESS 23.0.0)
  message(FATAL_ERROR "LLVM version ${LLVM_VERSION} is not supported. Please use LLVM 23.0.0 or later. Use LLVM_ROOT to specify a different LLVM installation.")
endif()

message(STATUS "FLANG_INSTALL_PREFIX  : ${FLANG_INSTALL_PREFIX}")
message(STATUS "FLANG_INCLUDE_DIRS    : ${FLANG_INCLUDE_DIRS}")

#===============================================================
# Create an Interface target for the Flang and LLVM Libraries
#===============================================================
add_library(flang_llvm INTERFACE)
if(LLVM_LINK_LLVM_DYLIB)
  target_link_libraries(flang_llvm INTERFACE flangFrontend MLIR $<$<PLATFORM_ID:Linux>:LLVM>)
else()
  target_link_libraries(flang_llvm INTERFACE flangFrontend MLIR $<$<PLATFORM_ID:Linux>:LLVMSupport>)
endif()
target_include_directories(flang_llvm SYSTEM INTERFACE ${FLANG_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(flang_llvm INTERFACE -fno-rtti)
endif()
