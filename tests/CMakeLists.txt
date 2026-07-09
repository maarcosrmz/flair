if(Python_Interpreter_FOUND)
  # prefer a repo-local venv (where pytest/numpy usually live) over the
  # CMake-detected interpreter
  set(FLAIR_TEST_PYTHON ${Python_EXECUTABLE})
  if(EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    set(FLAIR_TEST_PYTHON ${CMAKE_SOURCE_DIR}/.venv/bin/python)
  endif()

  add_test(
    NAME flair.pytest
    COMMAND ${FLAIR_TEST_PYTHON} -m pytest ${CMAKE_CURRENT_SOURCE_DIR} -ra
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(flair.pytest PROPERTIES
    ENVIRONMENT "FLAIR=$<TARGET_FILE:flair-f2py>")
endif()
