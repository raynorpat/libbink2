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
