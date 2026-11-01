# The macOS build of MechCommander Redux (Windows builds with src/MCRedux.slnx): SDL3 and mimalloc from src/lib,
# MCCore as a static library, the game (MechCommander Redux.app) and the test suite (mc_tests), with the sources the
# Visual Studio projects list. The GPU renderer runs on SDL GPU's Metal driver, with the HLSL shaders compiled to MSL.
#
#   brew install cmake glslang spirv-cross
#   cmake -S src/mac -B src/mac/build && cmake --build src/mac/build --parallel "$(sysctl -n hw.ncpu)"
cmake_minimum_required(VERSION 3.24)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "The oldest macOS the build runs on")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "The architectures to build (Apple Silicon)")
project(MCReduxMac LANGUAGES C CXX OBJC)

# The port uses C++23 throughout. Apple's libc++ has what it needs from Xcode 16.3 (AppleClang 17), with std::jthread
# and std::stop_token behind -fexperimental-library before Xcode 26.
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
  message(FATAL_ERROR "The Mac build needs Xcode 16.3 or later (AppleClang 17); this is AppleClang "
                      "${CMAKE_CXX_COMPILER_VERSION}.")
endif()

set(MC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${MC_SRC}/lib/SDL-release-3.4.12 sdl EXCLUDE_FROM_ALL)

set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# new and delete come from mimalloc-new-delete.h in MCAllocator.cpp, as on Windows.
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
add_subdirectory(${MC_SRC}/lib/mimalloc-3.3.1 mimalloc EXCLUDE_FROM_ALL)
# macOS's libSystem holds pthread, which SDL already links (-lpthread); mimalloc's own -lpthread after it only makes
# Apple's linker warn of a duplicate library.
get_target_property(MC_MIMALLOC_LINKS mimalloc-static INTERFACE_LINK_LIBRARIES)
list(FILTER MC_MIMALLOC_LINKS EXCLUDE REGEX "pthread")
set_target_properties(mimalloc-static PROPERTIES INTERFACE_LINK_LIBRARIES "${MC_MIMALLOC_LINKS}")

include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
set(MC_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

include(CheckCXXSourceCompiles)
set(MC_JTHREAD_TEST "#include <thread>\nint main() { std::jthread t([](std::stop_token) {}); t.request_stop(); }")
check_cxx_source_compiles("${MC_JTHREAD_TEST}" MC_HAS_JTHREAD)
set(MC_EXPERIMENTAL_LIBRARY "")

if(NOT MC_HAS_JTHREAD)
  set(MC_EXPERIMENTAL_LIBRARY -fexperimental-library)
endif()

# MCVersion.h and the app's Info.plist, from nbgv on every build as on Windows (version.cmake).
add_custom_target(mc_version
  COMMAND ${CMAKE_COMMAND} -DREPO_DIR=${MC_SRC}/.. -DOUTPUT=${MC_GENERATED_DIR}/MCVersion.h
          -DPLIST_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in -DPLIST=${MC_GENERATED_DIR}/Info.plist
          -DMIN_MACOS=${CMAKE_OSX_DEPLOYMENT_TARGET} -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
  BYPRODUCTS ${MC_GENERATED_DIR}/MCVersion.h ${MC_GENERATED_DIR}/Info.plist
  COMMENT "Getting the version from nbgv"
  VERBATIM)

# The GPU renderer's shaders: the HLSL MCCore.vcxproj compiles with dxc on Windows, compiled here with glslang (dxc
# has no macOS build), then turned into MSL with SDL GPU's Metal bindings by mc_msl.
find_program(MC_GLSLANG glslangValidator REQUIRED)
find_path(MC_SPIRV_CROSS_INCLUDE spirv_cross/spirv_msl.hpp REQUIRED)
find_library(MC_SPIRV_CROSS_MSL spirv-cross-msl REQUIRED)
find_library(MC_SPIRV_CROSS_GLSL spirv-cross-glsl REQUIRED)
find_library(MC_SPIRV_CROSS_CORE spirv-cross-core REQUIRED)

add_executable(mc_msl tools/mc_msl.cpp)
target_include_directories(mc_msl PRIVATE ${MC_SPIRV_CROSS_INCLUDE})
target_link_libraries(mc_msl PRIVATE ${MC_SPIRV_CROSS_MSL} ${MC_SPIRV_CROSS_GLSL} ${MC_SPIRV_CROSS_CORE})

set(MC_SHADER_HEADERS)

foreach(shader IN LISTS MCCORE_SHADERS)
  get_filename_component(name ${shader} NAME_WE)

  if(shader MATCHES "\\.pshader$")
    set(stage frag)
  else()
    set(stage vert)
  endif()

  set(spirv ${MC_GENERATED_DIR}/shaders/${name}.spv)
  set(header ${MC_GENERATED_DIR}/shaders/${name}.msl.h)
  add_custom_command(
    OUTPUT ${header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MC_GENERATED_DIR}/shaders
    COMMAND ${MC_GLSLANG} -D -V -e main -S ${stage} --target-env vulkan1.0 --quiet -o ${spirv} ${shader}
    COMMAND mc_msl ${spirv} ${header} MC_${name}_msl
    DEPENDS ${shader} mc_msl
    COMMENT "Compiling HLSL shader ${name} to MSL"
    VERBATIM)
  list(APPEND MC_SHADER_HEADERS ${header})
endforeach()

add_custom_target(mc_shaders DEPENDS ${MC_SHADER_HEADERS})

add_library(MCCore STATIC ${MCCORE_SOURCES})
add_dependencies(MCCore mc_shaders mc_version)
target_include_directories(MCCore PUBLIC ${MC_SRC}/lib/MCCore ${MC_SRC}/lib/mimalloc-3.3.1/include ${MC_GENERATED_DIR})
# bc7enc_rdo (the HD art pack's BC7 codec, sprite/MCBc7 wraps it): outside MCCore's project, as on Windows, and built
# without MCCore's precompiled header.
set(MC_BC7ENC_SOURCES ${MC_SRC}/lib/bc7enc_rdo/bc7enc.cpp ${MC_SRC}/lib/bc7enc_rdo/bc7decomp.cpp
                      ${MC_SRC}/lib/bc7enc_rdo/ert.cpp)
target_sources(MCCore PRIVATE ${MC_BC7ENC_SOURCES})
set_source_files_properties(${MC_BC7ENC_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS -w)
target_include_directories(MCCore PRIVATE ${MC_SRC}/lib/bc7enc_rdo)
# miniz (lib/MCInflate's DEFLATE), in C as on Windows, so a library of its own outside MCCore's C++ options.
add_library(mc_miniz STATIC ${MC_SRC}/lib/miniz-3.0.2/miniz.c)
target_compile_definitions(mc_miniz PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_TIME MINIZ_NO_STDIO
                                           MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_compile_options(mc_miniz PRIVATE -w)
target_include_directories(mc_miniz PUBLIC ${MC_SRC}/lib/miniz-3.0.2)
target_link_libraries(MCCore PUBLIC mc_miniz)
# dr_flac: one portable C decoder, outside MCCore's precompiled C++ header, as on Windows.
add_library(mc_flac STATIC ${MC_SRC}/lib/dr_flac/dr_flac.c)
target_compile_options(mc_flac PRIVATE -w)
target_include_directories(mc_flac PUBLIC ${MC_SRC}/lib/dr_flac)
target_link_libraries(MCCore PUBLIC mc_flac)
# dav1d (the HD art pack's movies' AV1 decoder): its C code alone, as on Windows, the files MCCore.vcxproj lists; the
# template files are built for 8 bits a sample (src/lib/dav1d-1.5.4/README.md).
set(MC_DAV1D_DIR ${MC_SRC}/lib/dav1d-1.5.4)
mc_vcxproj_items(MC_DAV1D_ITEMS ${MC_SRC}/lib/MCCore/MCCore.vcxproj "" ClCompile
                 "\\$\\(MCLibDir\\)dav1d-1\\.5\\.4[^\"]+\\.c")
list(TRANSFORM MC_DAV1D_ITEMS REPLACE "^/\\$\\(MCLibDir\\)" "${MC_SRC}/lib/")
add_library(mc_dav1d STATIC ${MC_DAV1D_ITEMS})
list(FILTER MC_DAV1D_ITEMS INCLUDE REGEX "_tmpl\\.c$")
set_source_files_properties(${MC_DAV1D_ITEMS} PROPERTIES COMPILE_DEFINITIONS BITDEPTH=8)
set_target_properties(mc_dav1d PROPERTIES C_STANDARD 11)
target_compile_options(mc_dav1d PRIVATE -w)
target_include_directories(mc_dav1d PRIVATE ${MC_DAV1D_DIR} PUBLIC ${MC_DAV1D_DIR}/include)
target_link_libraries(MCCore PUBLIC mc_dav1d)
target_precompile_headers(MCCore PRIVATE ${MC_SRC}/lib/MCCore/stdafx.h)
target_link_libraries(MCCore PUBLIC SDL3::SDL3-static mimalloc-static)
# Apple's clang fuses a*b+c into one rounding where MSVC rounds twice; off, the game computes what it does on Windows
# (the tests' recorded runs, and multiplayer with Windows players).
target_compile_options(MCCore PUBLIC -ffp-contract=off ${MC_EXPERIMENTAL_LIBRARY})
target_link_options(MCCore PUBLIC ${MC_EXPERIMENTAL_LIBRARY})

# The files Visual Studio builds without the precompiled header, because Windows.h (or here CoreGraphics) and MCCore's
# Win32 stand-ins can't meet.
set(MC_APP_NO_PCH ${MC_SRC}/apps/MCRedux/MCConsole.cpp ${MC_SRC}/apps/MCRedux/MCCrashTrace.cpp
                  ${MC_SRC}/apps/MCRedux/MCGameFolder.cpp)
set_source_files_properties(${MC_APP_NO_PCH} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# The game, as "MechCommander Redux.app": upstream's icon at the Mac's sizes (src/res/MCRedux.icns), and the
# Info.plist version.cmake writes, put in after linking and sealed with an ad-hoc signature, which Apple silicon needs
# to run it at all.
set(MC_ICON ${MC_SRC}/res/MCRedux.icns)
set_source_files_properties(${MC_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
add_executable(MCRedux MACOSX_BUNDLE ${MCREDUX_SOURCES} ${MC_ICON})
# The Info.plist is a link dependency, so a changed one (a new version, an edited template) relinks, which copies it.
set_target_properties(MCRedux PROPERTIES OUTPUT_NAME "MechCommander Redux" LINK_DEPENDS ${MC_GENERATED_DIR}/Info.plist)
target_include_directories(MCRedux PRIVATE ${MC_SRC}/apps/MCRedux)
target_precompile_headers(MCRedux PRIVATE ${MC_SRC}/apps/MCRedux/stdafx.h)
target_link_libraries(MCRedux PRIVATE MCCore "-framework CoreGraphics")
add_custom_command(TARGET MCRedux POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MC_GENERATED_DIR}/Info.plist
          $<TARGET_BUNDLE_CONTENT_DIR:MCRedux>/Info.plist
  COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:MCRedux>
  COMMENT "Signing MechCommander Redux.app (ad hoc)"
  VERBATIM)

add_executable(mc_tests ${MCTESTS_SOURCES})
target_include_directories(mc_tests PRIVATE ${MC_SRC}/apps/MCRedux)
target_precompile_headers(mc_tests PRIVATE ${MC_SRC}/apps/MCRedux/stdafx.h)
target_link_libraries(mc_tests PRIVATE MCCore "-framework CoreGraphics")
