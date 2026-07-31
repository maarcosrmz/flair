function(add_tool TOOL_NAME)
  if(NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${TOOL_NAME})
    message(FATAL_ERROR "Tool directory '${TOOL_NAME}' does not exist.")
  endif()

  file(GLOB_RECURSE TOOL_SOURCES "${TOOL_NAME}/*.cpp")

  add_executable(${TOOL_NAME} ${TOOL_SOURCES})
  
  # Enable warnings
  target_link_libraries(${TOOL_NAME} PRIVATE $<BUILD_INTERFACE:flair_warnings>)
  
  # Configure annoteate tool target
  target_compile_features(${TOOL_NAME} PRIVATE cxx_std_17)
  target_include_directories(${TOOL_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  # Reported by --version. flair_VERSION rather than PROJECT_VERSION, which a
  # FetchContent'd subproject can shadow.
  target_compile_definitions(${TOOL_NAME} PRIVATE
    FLAIR_VERSION="${flair_VERSION}" GIT_HASH="${PROJECT_GIT_HASH}")
  # clang_llvm provides clang::tooling (compile_commands.json loading)
  target_link_libraries(${TOOL_NAME} PRIVATE flu fmt::fmt flang_llvm clang_llvm)
  # LLVM/MLIR/Clang are linked from outside the build tree (LLVM_ROOT), and
  # CMake drops the build rpath on install: without USE_LINK_PATH the installed
  # tool cannot find libLLVM.so/libMLIR.so/libclang-cpp.so.
  set_target_properties(${TOOL_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    INSTALL_RPATH_USE_LINK_PATH ON)

  # Install the tool
  install(TARGETS ${TOOL_NAME} DESTINATION bin)
endfunction()

add_tool(flair-f2py)
