cmake_minimum_required(VERSION 3.16)
project(libbink2 VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(BINK2_BUILD_STATIC "Build the static libbink2 library" ON)
option(BINK2_BUILD_SHARED "Build the shared libbink2 library" ON)
option(BINK2_BUILD_ENCODER "Build the libbink2 encoder library" OFF)
option(BINK2_BUILD_TOOLS "Build the example programs" OFF)
option(BINK2_BUILD_TESTS "Build the test programs" ON)
set(BINK2_SDL_VERSION "AUTO" CACHE STRING
  "SDL version for bink2_sdl_player: AUTO, 2 or 3")
set_property(CACHE BINK2_SDL_VERSION PROPERTY STRINGS AUTO 2 3)

function(bink2_set_compile_settings target)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /fp:precise)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -ffp-contract=off)
  endif()
endfunction()

set(BINK2_DECODER_SOURCES src/bink2.c src/bink2_bits.c)

if(BINK2_BUILD_STATIC)
  add_library(bink2_static STATIC ${BINK2_DECODER_SOURCES})
  bink2_set_compile_settings(bink2_static)
  target_include_directories(bink2_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  set_target_properties(bink2_static PROPERTIES OUTPUT_NAME bink2)
  if(NOT MSVC)
    target_link_libraries(bink2_static PUBLIC m)
  endif()
endif()

if(BINK2_BUILD_SHARED)
  add_library(bink2_shared SHARED ${BINK2_DECODER_SOURCES})
  bink2_set_compile_settings(bink2_shared)
  target_include_directories(bink2_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  set_target_properties(bink2_shared PROPERTIES
    OUTPUT_NAME bink2
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
  if(NOT MSVC)
    target_link_libraries(bink2_shared PUBLIC m)
  endif()
endif()

if(BINK2_BUILD_SHARED)
  set(BINK2_LINK_TARGET bink2_shared)
elseif(BINK2_BUILD_STATIC)
  set(BINK2_LINK_TARGET bink2_static)
else()
  message(FATAL_ERROR "Enable BINK2_BUILD_STATIC or BINK2_BUILD_SHARED")
endif()
add_library(Bink2::bink2 ALIAS ${BINK2_LINK_TARGET})

if(BINK2_BUILD_ENCODER)
  message(STATUS "BINK2_BUILD_ENCODER is not implemented yet")
endif()

if(BINK2_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BINK2_BUILD_STATIC)
  install(TARGETS bink2_static
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(BINK2_BUILD_SHARED)
  install(TARGETS bink2_shared
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

install(FILES src/bink2.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
