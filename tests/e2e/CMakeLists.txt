# Olive - Non-Linear Video Editor
# End-to-End Test Suite (tests/e2e/CMakeLists.txt)

olive_add_test(E2E e2e-audio-tests e2e_audio_tests.cpp)
olive_add_test(E2E e2e-scenecut-tests e2e_scenecut_tests.cpp)
olive_add_test(E2E e2e-interchange-tests e2e_interchange_tests.cpp)
olive_add_test(E2E e2e-workflow-tests e2e_workflow_tests.cpp)

target_compile_definitions(e2e-workflow-tests PRIVATE APP_DIR_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(e2e-audio-tests PRIVATE APP_DIR_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(e2e-scenecut-tests PRIVATE APP_DIR_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(e2e-interchange-tests PRIVATE APP_DIR_ROOT="${CMAKE_SOURCE_DIR}")

find_program(OLIVE_TEST_FFMPEG ffmpeg)
if(OLIVE_TEST_FFMPEG)
  target_compile_definitions(e2e-scenecut-tests PRIVATE OLIVE_TEST_FFMPEG="${OLIVE_TEST_FFMPEG}")
  target_compile_definitions(e2e-workflow-tests PRIVATE OLIVE_TEST_FFMPEG="${OLIVE_TEST_FFMPEG}")
endif()

# Register Python E2E packaging test in CTest under the E2E label
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
  NAME e2e-packaging-validation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_packaging.py
)
set_tests_properties(e2e-packaging-validation PROPERTIES TIMEOUT 60 LABELS "E2E")
