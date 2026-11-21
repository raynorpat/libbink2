add_executable(test_api test_api.c)
target_link_libraries(test_api PRIVATE ${BINK2_LINK_TARGET})
target_compile_definitions(test_api PRIVATE
  BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
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

add_executable(test_thread test_thread.c)
target_link_libraries(test_thread PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME thread COMMAND test_thread)

add_executable(test_dsp_simd test_dsp_simd.c)
target_link_libraries(test_dsp_simd PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME dsp_simd COMMAND test_dsp_simd)

add_executable(test_filter test_filter.c)
target_link_libraries(test_filter PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME filter COMMAND test_filter)

add_executable(test_filter_decode test_filter_decode.c)
target_link_libraries(test_filter_decode PRIVATE ${BINK2_LINK_TARGET})
target_compile_definitions(test_filter_decode PRIVATE
  BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
add_test(NAME filter_decode COMMAND test_filter_decode)
set_tests_properties(filter_decode PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_video test_video.c)
target_link_libraries(test_video PRIVATE ${BINK2_LINK_TARGET})
target_compile_definitions(test_video PRIVATE
  BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
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
target_compile_definitions(test_malformed PRIVATE
  BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
add_test(NAME malformed COMMAND test_malformed)

add_executable(test_audio_unit test_audio_unit.c)
target_link_libraries(test_audio_unit PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME audio_unit COMMAND test_audio_unit)

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

if(TARGET bink2_bench)
  add_test(NAME bench_smoke COMMAND bink2_bench --iterations 2 --rgb
    ${CMAKE_CURRENT_SOURCE_DIR}/media/synthetic_kb2j.bk2)
  set_tests_properties(bench_smoke PROPERTIES PASS_REGULAR_EXPRESSION
    "frames=[0-9]+ seconds=[0-9.]+ fps=[0-9.]+")
endif()

add_executable(test_player_motion test_player_motion.c
  ${CMAKE_SOURCE_DIR}/src/tools/player/bink2_motion.c)
target_include_directories(test_player_motion PRIVATE
  ${CMAKE_SOURCE_DIR}/src/tools/player)
target_link_libraries(test_player_motion PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME player_motion COMMAND test_player_motion)

add_executable(test_player_launcher test_player_launcher.c)
foreach(player sdl sdlgl sdlmetal sdlvulkan d3d9 d3d11)
  if(TARGET bink2_${player}_player)
    add_test(NAME ${player} COMMAND test_player_launcher
      $<TARGET_FILE:bink2_${player}_player>)
    set_tests_properties(${player} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  endif()
endforeach()

if(TARGET bink2_encoder)
  add_executable(test_enc_bits test_enc_bits.c)
  target_link_libraries(test_enc_bits PRIVATE bink2_encoder)
  add_test(NAME enc_bits COMMAND test_enc_bits)

  add_executable(test_enc_wav test_enc_wav.c)
  target_link_libraries(test_enc_wav PRIVATE bink2_encoder)
  add_test(NAME enc_wav COMMAND test_enc_wav)

  add_executable(test_enc_audio test_enc_audio.c)
  target_link_libraries(test_enc_audio PRIVATE bink2_encoder)
  add_test(NAME enc_audio COMMAND test_enc_audio)

  add_executable(test_enc_container_audio test_enc_container_audio.c)
  target_link_libraries(test_enc_container_audio PRIVATE bink2_encoder)
  add_test(NAME enc_container_audio COMMAND test_enc_container_audio)

  add_executable(test_enc_audio_file test_enc_audio_file.c)
  target_link_libraries(test_enc_audio_file PRIVATE bink2_encoder)
  add_test(NAME enc_audio_file COMMAND test_enc_audio_file)

  add_executable(test_enc_audio_oracle test_enc_audio_oracle.c)
  target_link_libraries(test_enc_audio_oracle PRIVATE bink2_encoder)
  add_test(NAME enc_audio_oracle COMMAND test_enc_audio_oracle)
  set_tests_properties(enc_audio_oracle PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(test_enc_image test_enc_image.c)
  target_link_libraries(test_enc_image PRIVATE bink2_encoder)
  target_compile_definitions(test_enc_image PRIVATE
    BINK2_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
  add_test(NAME enc_image COMMAND test_enc_image)

  add_executable(test_threads test_threads.c)
  target_link_libraries(test_threads PRIVATE bink2_encoder)
  add_test(NAME threads COMMAND test_threads)

  add_executable(test_enc_options test_enc_options.c)
  target_link_libraries(test_enc_options PRIVATE bink2_encoder)
  add_test(NAME enc_options COMMAND test_enc_options)

  add_executable(test_enc_rate test_enc_rate.c)
  target_link_libraries(test_enc_rate PRIVATE bink2_encoder)
  add_test(NAME enc_rate COMMAND test_enc_rate)

  add_executable(test_enc_inter test_enc_inter.c)
  target_link_libraries(test_enc_inter PRIVATE bink2_encoder)
  add_test(NAME enc_inter COMMAND test_enc_inter)

  add_executable(test_enc_intra test_enc_intra.c)
  target_link_libraries(test_enc_intra PRIVATE bink2_encoder)
  add_test(NAME enc_intra COMMAND test_enc_intra)

  add_executable(test_enc_syntax test_enc_syntax.c)
  target_link_libraries(test_enc_syntax PRIVATE bink2_encoder)
  add_test(NAME enc_syntax COMMAND test_enc_syntax)

  add_executable(test_enc_dct test_enc_dct.c)
  target_link_libraries(test_enc_dct PRIVATE bink2_encoder)
  add_test(NAME enc_dct COMMAND test_enc_dct)

  add_executable(test_block_info test_block_info.c)
  target_link_libraries(test_block_info PRIVATE bink2_encoder)
  add_test(NAME block_info COMMAND test_block_info)

  add_executable(test_enc_api test_enc_api.c)
  target_link_libraries(test_enc_api PRIVATE bink2_encoder)
  add_test(NAME enc_api COMMAND test_enc_api)
endif()

if(TARGET bink2_build)
  add_test(NAME build_help COMMAND bink2_build --help)

  add_executable(test_build_e2e test_build_e2e.c)
  target_link_libraries(test_build_e2e PRIVATE bink2_encoder)
  target_compile_definitions(test_build_e2e PRIVATE
    BINK2_BUILD_EXE="$<TARGET_FILE:bink2_build>")
  add_test(NAME build_e2e COMMAND test_build_e2e)
endif()
