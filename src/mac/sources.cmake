# The source lists are read from the Visual Studio projects, so the Mac build compiles exactly what the Windows build
# does and follows files being added or removed there. CMake configures again whenever a project file changes.

# The items of kind <item> (ClCompile, CustomBuild, ...) in <project_file> whose path matches <pattern>, as full paths.
function(mc_vcxproj_items out project_file base_dir item pattern)
  file(READ "${project_file}" text)
  string(REGEX MATCHALL "<${item} Include=\"${pattern}\"" items "${text}")

  if(NOT items)
    message(FATAL_ERROR "No ${item} items matching ${pattern} in ${project_file}")
  endif()

  set(paths)

  foreach(entry IN LISTS items)
    string(REGEX REPLACE "<${item} Include=\"([^\"]+)\"" "\\1" path "${entry}")
    string(REPLACE "\\" "/" path "${path}")
    list(APPEND paths "${base_dir}/${path}")
  endforeach()

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project_file}")
  set(${out} ${paths} PARENT_SCOPE)
endfunction()

# The C++ sources. Entries under $(MCLibDir) (mimalloc's static.c) are left out: the mimalloc subdirectory builds them.
function(mc_vcxproj_sources out project_file base_dir)
  mc_vcxproj_items(paths "${project_file}" "${base_dir}" ClCompile "[^\"$]+\\.cpp")
  set(${out} ${paths} PARENT_SCOPE)
endfunction()

mc_vcxproj_sources(MCCORE_SOURCES ${MC_SRC}/lib/MCCore/MCCore.vcxproj ${MC_SRC}/lib/MCCore)
mc_vcxproj_sources(MCREDUX_SOURCES ${MC_SRC}/apps/MCRedux/MCRedux.vcxproj ${MC_SRC}/apps/MCRedux)
mc_vcxproj_sources(MCTESTS_SOURCES ${MC_SRC}/apps/MCRedux/mc_tests.vcxproj ${MC_SRC}/apps/MCRedux)

# The GPU renderer's HLSL shaders, which MCCore.vcxproj compiles with dxc.
mc_vcxproj_items(MCCORE_SHADERS ${MC_SRC}/lib/MCCore/MCCore.vcxproj ${MC_SRC}/lib/MCCore CustomBuild
                 "shaders\\\\[^\"]+\\.[pv]shader")
# What they include beside each other (pixels.hlsli).
mc_vcxproj_items(MCCORE_SHADER_INCLUDES ${MC_SRC}/lib/MCCore/MCCore.vcxproj ${MC_SRC}/lib/MCCore None
                 "shaders\\\\[^\"]+\\.hlsli")
