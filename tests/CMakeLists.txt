add_executable(test_api test_api.c)
target_link_libraries(test_api PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME api COMMAND test_api)

add_executable(test_bits test_bits.c)
target_link_libraries(test_bits PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME bits COMMAND test_bits)

add_executable(test_container test_container.c)
target_link_libraries(test_container PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME container COMMAND test_container)

add_executable(test_frames test_frames.c)
target_link_libraries(test_frames PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME frames COMMAND test_frames)

add_executable(test_video test_video.c)
target_link_libraries(test_video PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME video COMMAND test_video)

add_executable(test_decode_f test_decode_f.c)
target_link_libraries(test_decode_f PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME decode_f COMMAND test_decode_f)
set_tests_properties(decode_f PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_decode_g test_decode_g.c)
target_link_libraries(test_decode_g PRIVATE ${BINK2_LINK_TARGET})
target_compile_definitions(test_decode_g PRIVATE
  BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
add_test(NAME decode_g COMMAND test_decode_g)
set_tests_properties(decode_g PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_malformed test_malformed.c)
target_link_libraries(test_malformed PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME malformed COMMAND test_malformed)

add_executable(test_audio test_audio.c)
target_link_libraries(test_audio PRIVATE ${BINK2_LINK_TARGET})
target_compile_definitions(test_audio PRIVATE
  BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
add_test(NAME audio COMMAND test_audio)
set_tests_properties(audio PROPERTIES SKIP_RETURN_CODE 77)

if(TARGET bink2_dump)
  add_test(NAME dump COMMAND ${CMAKE_COMMAND}
    -DDUMP_EXE=$<TARGET_FILE:bink2_dump>
    -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/dump-test
    -DDUMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/dump-test
    -DCHECK_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/check_dump.cmake
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_dump.cmake)
  set_tests_properties(dump PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(TARGET bink2_sdl_player)
  add_test(NAME sdl COMMAND ${CMAKE_COMMAND}
    -DPLAYER_EXE=$<TARGET_FILE:bink2_sdl_player>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_sdl.cmake)
  set_tests_properties(sdl PROPERTIES SKIP_RETURN_CODE 77)
endif()
