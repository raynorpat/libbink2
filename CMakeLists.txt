cmake_minimum_required(VERSION 3.16)
project(libbink2 VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BINK2_BUILD_STATIC "Build the static libbink2 library" ON)
option(BINK2_BUILD_SHARED "Build the shared libbink2 library" ON)
option(BINK2_BUILD_ENCODER "Build the libbink2 encoder library" ON)
option(BINK2_BUILD_TOOLS "Build the example programs" OFF)
option(BINK2_BUILD_TESTS "Build the test programs" ON)
option(BINK2_BUILD_PLAYER_OPENGL "Build the SDL + OpenGL player" ON)
option(BINK2_BUILD_PLAYER_METAL "Build the SDL + Metal player" ON)
option(BINK2_BUILD_PLAYER_VULKAN "Build the SDL + Vulkan player" ON)
option(BINK2_BUILD_PLAYER_D3D9 "Build the Direct3D 9 player" ON)
option(BINK2_BUILD_PLAYER_D3D11 "Build the Direct3D 11 player" ON)
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
    target_compile_options(${target} PRIVATE -Wall -Wextra -fno-math-errno -fno-signed-zeros)
  endif()
endfunction()

set(BINK2_DECODER_SOURCES
  src/bink2.c src/bink2_bits.c src/bink2_dsp.c src/bink2_tables.c
  src/bink2_video.c src/bink2_video_old.c src/bink2_video_new.c
  src/bink2_audio.c src/bink2_thread.c)

find_package(Threads REQUIRED)

if(BINK2_BUILD_STATIC)
  add_library(bink2_static STATIC ${BINK2_DECODER_SOURCES})
  bink2_set_compile_settings(bink2_static)
  target_include_directories(bink2_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  set_target_properties(bink2_static PROPERTIES OUTPUT_NAME bink2)
  target_link_libraries(bink2_static PUBLIC Threads::Threads)
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
  target_link_libraries(bink2_shared PRIVATE Threads::Threads)
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

if(BINK2_BUILD_TOOLS)
  add_executable(bink2_dump src/tools/bink2_dump.c)
  bink2_set_compile_settings(bink2_dump)
  target_link_libraries(bink2_dump PRIVATE ${BINK2_LINK_TARGET})

  add_executable(bink2_bench src/tools/bink2_bench.c)
  bink2_set_compile_settings(bink2_bench)
  target_link_libraries(bink2_bench PRIVATE ${BINK2_LINK_TARGET})

  if(NOT CMAKE_CROSSCOMPILING OR DEFINED SDL3_DIR OR DEFINED SDL2_DIR)
    if(NOT BINK2_SDL_VERSION MATCHES "^(AUTO|2|3)$")
      message(FATAL_ERROR
        "BINK2_SDL_VERSION must be AUTO, 2 or 3 (got '${BINK2_SDL_VERSION}')")
    endif()
    set(BINK2_SDL_TARGET "")
    set(BINK2_USE_SDL3 OFF)
    if(BINK2_SDL_VERSION STREQUAL "AUTO" OR BINK2_SDL_VERSION STREQUAL "3")
      find_package(SDL3 QUIET CONFIG)
      if(SDL3_FOUND)
        set(BINK2_SDL_TARGET SDL3::SDL3)
        set(BINK2_USE_SDL3 ON)
      elseif(BINK2_SDL_VERSION STREQUAL "3")
        message(FATAL_ERROR "BINK2_SDL_VERSION=3 but no SDL3 package was found")
      endif()
    endif()
    if(BINK2_SDL_TARGET STREQUAL "" AND
        (BINK2_SDL_VERSION STREQUAL "AUTO" OR BINK2_SDL_VERSION STREQUAL "2"))
      find_package(SDL2 QUIET CONFIG)
      if(SDL2_FOUND)
        set(BINK2_SDL_TARGET SDL2::SDL2)
      elseif(BINK2_SDL_VERSION STREQUAL "2")
        message(FATAL_ERROR "BINK2_SDL_VERSION=2 but no SDL2 package was found")
      endif()
    endif()
    if(NOT BINK2_SDL_TARGET STREQUAL "")
      add_executable(bink2_sdl_player src/tools/bink2_sdl_player.c
                     src/tools/bink2_mix.c)
      bink2_set_compile_settings(bink2_sdl_player)
      if(BINK2_USE_SDL3)
        target_compile_definitions(bink2_sdl_player PRIVATE BINK2_USE_SDL3=1)
      elseif(TARGET SDL2::SDL2main)
        target_link_libraries(bink2_sdl_player PRIVATE SDL2::SDL2main)
      endif()
      target_link_libraries(bink2_sdl_player PRIVATE
        ${BINK2_LINK_TARGET} ${BINK2_SDL_TARGET})
      install(TARGETS bink2_sdl_player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

      set(BINK2_PLAYER_COMMON
        src/tools/player/bink2_sdl_player_core.c
        src/tools/player/bink2_playback.c
        src/tools/player/bink2_playback_sdl.c
        src/tools/player/bink2_mix.c
        src/tools/player/bink2_motion.c)

      function(bink2_add_sdl_player target)
        add_executable(${target} ${ARGN} ${BINK2_PLAYER_COMMON})
        bink2_set_compile_settings(${target})
        if(BINK2_USE_SDL3)
          target_compile_definitions(${target} PRIVATE BINK2_USE_SDL3=1)
        elseif(TARGET SDL2::SDL2main)
          target_link_libraries(${target} PRIVATE SDL2::SDL2main)
        endif()
        target_link_libraries(${target} PRIVATE
          ${BINK2_LINK_TARGET} ${BINK2_SDL_TARGET})
        install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
      endfunction()

      if(BINK2_BUILD_PLAYER_OPENGL)
        bink2_add_sdl_player(bink2_sdlgl_player
          src/tools/player/bink2_sdlgl_player.c
          src/tools/player/bink2_gl_renderer.c)
        if(APPLE)
          target_compile_options(bink2_sdlgl_player PRIVATE
            -Wno-deprecated-declarations)
        endif()
      endif()

      if(APPLE AND BINK2_BUILD_PLAYER_METAL)
        enable_language(OBJC)
        bink2_add_sdl_player(bink2_sdlmetal_player
          src/tools/player/bink2_sdlmetal_player.c
          src/tools/player/bink2_metal_renderer.m)
        target_link_libraries(bink2_sdlmetal_player PRIVATE
          "-framework Metal" "-framework QuartzCore" "-framework Foundation")
      endif()

      if(BINK2_BUILD_PLAYER_VULKAN)
        find_package(Vulkan QUIET)
        find_path(BINK2_SHADERC_INCLUDE shaderc/shaderc.h)
        find_library(BINK2_SHADERC_LIBRARY NAMES shaderc_shared
          shaderc_combined shaderc)
        if(Vulkan_FOUND AND BINK2_SHADERC_INCLUDE AND BINK2_SHADERC_LIBRARY)
          bink2_add_sdl_player(bink2_sdlvulkan_player
            src/tools/player/bink2_sdlvulkan_player.c
            src/tools/player/bink2_vulkan_renderer.c)
          target_include_directories(bink2_sdlvulkan_player PRIVATE
            ${BINK2_SHADERC_INCLUDE})
          target_link_libraries(bink2_sdlvulkan_player PRIVATE
            Vulkan::Vulkan ${BINK2_SHADERC_LIBRARY})
          if(BINK2_SHADERC_LIBRARY MATCHES "\\.a$" AND APPLE)
            target_link_libraries(bink2_sdlvulkan_player PRIVATE c++)
          endif()
        else()
          message(STATUS "Vulkan or shaderc not found; skipping bink2_sdlvulkan_player")
        endif()
      endif()
    else()
      message(STATUS "SDL2/SDL3 not found; skipping bink2_sdl_player")
    endif()
  endif()
endif()

if(WIN32 AND BINK2_BUILD_TOOLS)
  set(BINK2_D3D_COMMON
    src/tools/player/bink2_win32_player.c
    src/tools/player/bink2_playback.c
    src/tools/player/bink2_playback_win32.c
    src/tools/player/bink2_mix.c
    src/tools/player/bink2_motion.c)
  foreach(api d3d9 d3d11)
    string(TOUPPER "${api}" api_upper)
    if(BINK2_BUILD_PLAYER_${api_upper})
      add_executable(bink2_${api}_player ${BINK2_D3D_COMMON}
        src/tools/player/bink2_${api}_renderer.c)
      bink2_set_compile_settings(bink2_${api}_player)
      target_compile_definitions(bink2_${api}_player PRIVATE
        BINK2_${api_upper}=1)
      target_link_libraries(bink2_${api}_player PRIVATE
        ${BINK2_LINK_TARGET} ${api} dxguid d3dcompiler winmm)
      install(TARGETS bink2_${api}_player RUNTIME DESTINATION
        ${CMAKE_INSTALL_BINDIR})
    endif()
  endforeach()
endif()

if(BINK2_BUILD_ENCODER)
  set(BINK2_ENCODER_SOURCES
    src/encoder/bink2_encoder.c
    src/encoder/bink2_enc_bits.c
    src/encoder/bink2_enc_dct.c
    src/encoder/bink2_enc_syntax.c
    src/encoder/bink2_enc_container.c
    src/encoder/bink2_enc_video.c
    src/encoder/bink2_enc_motion.c
    src/encoder/bink2_enc_audio.c
    src/encoder/bink2_enc_wav.c
    src/encoder/bink2_enc_image.c
    src/encoder/bink2_enc_stb.c)
  add_library(bink2_encoder STATIC ${BINK2_ENCODER_SOURCES})
  bink2_set_compile_settings(bink2_encoder)
  target_include_directories(bink2_encoder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/encoder>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_include_directories(bink2_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
  target_link_libraries(bink2_encoder PUBLIC ${BINK2_LINK_TARGET})
  set_target_properties(bink2_encoder PROPERTIES OUTPUT_NAME bink2enc)
  add_library(Bink2::encoder ALIAS bink2_encoder)
endif()

if(BINK2_BUILD_TOOLS AND TARGET bink2_encoder)
  add_executable(bink2_build src/tools/bink2_build.c)
  bink2_set_compile_settings(bink2_build)
  target_link_libraries(bink2_build PRIVATE bink2_encoder)
  install(TARGETS bink2_build RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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

if(BINK2_BUILD_ENCODER)
  install(TARGETS bink2_encoder
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(FILES src/encoder/bink2_encoder.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(BINK2_BUILD_TOOLS)
  install(TARGETS bink2_dump RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
