if(NOT DEFINED PLAYER_EXE OR NOT EXISTS "${PLAYER_EXE}")
  message(FATAL_ERROR "bink2_sdl_player executable not found: ${PLAYER_EXE}")
endif()
if(NOT DEFINED ENV{BINK2_TEST_MEDIA} OR "$ENV{BINK2_TEST_MEDIA}" STREQUAL "")
  message(STATUS "SKIP: BINK2_TEST_MEDIA not set")
  return()
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          BINK2_PLAYER_TEST_FRAMES=3
          "${PLAYER_EXE}" "$ENV{BINK2_TEST_MEDIA}/ge_video_86l.bk2"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 77)
  message(STATUS "SKIP: player could not create a dummy window")
  return()
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bink2_sdl_player failed (${result}): ${output}${error}")
endif()
