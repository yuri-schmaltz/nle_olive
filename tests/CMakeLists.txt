# Olive - Non-Linear Video Editor
# Copyright (C) 2022 Olive Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

function(olive_add_test GROUP NAME SOURCE)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${SOURCE}
  )

  file(READ "${SOURCE}" TEST_FILE_CONTENT)
  string(REGEX MATCHALL "OLIVE_ADD_TEST\(.[A-Za-z0-9_]+\)" TEST_FUNCTIONS ${TEST_FILE_CONTENT})
  set(TEST_BODY "#include <QCoreApplication>\n#include <QStandardPaths>\nint main(int argc, char** argv)\n{\n  QCoreApplication app(argc, argv);\n  QStandardPaths::setTestModeEnabled(true);\n  int ret;(void)ret;")
  if(ARGC GREATER 3 AND ARGV3 STREQUAL "GPU")
    string(REPLACE "#include <QCoreApplication>" "#include <QGuiApplication>\n#include <QSurfaceFormat>" TEST_BODY "${TEST_BODY}")
    string(REPLACE "QCoreApplication app(argc, argv);" "QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile); QSurfaceFormat::setDefaultFormat(format); QGuiApplication app(argc, argv);" TEST_BODY "${TEST_BODY}")
  endif()
  set(TEST_INDEX 1)
  list(LENGTH TEST_FUNCTIONS TEST_COUNT)
  if (TEST_COUNT EQUAL 0)
    message(STATUS "Skipping ${NAME}: no test cases registered")
    return()
  endif()
  foreach (TEST_FUNC ${TEST_FUNCTIONS})
    string(REPLACE "OLIVE_ADD_TEST(" "" TEST_FUNC "${TEST_FUNC}")
    string(APPEND TEST_BODY "  std::cout << \"[${TEST_INDEX}/${TEST_COUNT}] ${GROUP} - ${TEST_FUNC}\";\n")
    string(APPEND TEST_BODY "  if ((ret = olive::Test${TEST_FUNC}()) == OLIVE_TEST_SUCCESS) {std::cout << \" - PASSED\" << std::endl;}else{std::cout << \" - FAILED AT LINE \" << ret << std::endl;return 1;}\n")
    MATH(EXPR TEST_INDEX "${TEST_INDEX}+1")
  endforeach()
  string(APPEND TEST_BODY "  return 0;\n}")

  set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SOURCE}")

  string(APPEND TEST_FILE_CONTENT "\n${TEST_BODY}")
  file(WRITE "${OUTPUT_FILE}" "${TEST_FILE_CONTENT}")

  add_executable(${NAME} ${OUTPUT_FILE} $<TARGET_OBJECTS:libolive-editor>)
  target_include_directories(
    ${NAME}
    PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/tests
    ${OLIVE_INCLUDE_DIRS}
  )
  target_link_libraries(
    ${NAME}
    PRIVATE
    ${OLIVE_LIBRARIES}
  )
  target_compile_definitions(
    ${NAME}
    PRIVATE
    ${OLIVE_DEFINITIONS}
  )
  target_compile_options(
    ${NAME}
    PRIVATE
    ${OLIVE_COMPILE_OPTIONS}
  )
  if (MSVC)
    add_test("Olive.${GROUP}.${NAME}" ${NAME})
  else()
    add_test(${NAME} ${NAME})
  endif()
  if(MSVC)
    set(test_name "Olive.${GROUP}.${NAME}")
  else()
    set(test_name "${NAME}")
  endif()
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 120 LABELS "${GROUP}")
endfunction()

add_subdirectory(compositing)
add_subdirectory(general)
add_subdirectory(timeline)
add_subdirectory(task)

add_subdirectory(project)
add_subdirectory(render)

add_subdirectory(export)
add_subdirectory(e2e)

olive_add_test(Node equalizer-tests node/equalizer-tests.cpp)
olive_add_test(Timeline track-audio-tests timeline/track-audio-tests.cpp)
