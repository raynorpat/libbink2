add_executable(test_api test_api.c)
target_link_libraries(test_api PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME api COMMAND test_api)

add_executable(test_bits test_bits.c)
target_link_libraries(test_bits PRIVATE ${BINK2_LINK_TARGET})
add_test(NAME bits COMMAND test_bits)
