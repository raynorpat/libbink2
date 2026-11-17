if(NOT DEFINED DUMP_EXE OR NOT EXISTS "${DUMP_EXE}")
  message(FATAL_ERROR "bink2_dump executable not found: ${DUMP_EXE}")
endif()
if(NOT DEFINED OUT_DIR OR NOT DEFINED CHECK_SCRIPT)
  message(FATAL_ERROR "OUT_DIR and CHECK_SCRIPT are required")
endif()
if(NOT DEFINED ENV{BINK2_TEST_MEDIA} OR "$ENV{BINK2_TEST_MEDIA}" STREQUAL "")
  message(STATUS "SKIP: BINK2_TEST_MEDIA not set")
  return()
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DUMP_EXE}" --frames 2
          "$ENV{BINK2_TEST_MEDIA}/ge_video_86l.bk2" "${OUT_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bink2_dump failed (${result}): ${output}${error}")
endif()

include("${CHECK_SCRIPT}")
