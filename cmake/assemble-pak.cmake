# Assembles the TrimUI pak layout: /SDCARD/Apps/MRG.pak/ with the executable,
# resources, cores and the launcher metadata. Driven by the pak
# target in CMakeLists.txt, which passes every path in, so this script also
# works standalone when debugging the layout.

if(NOT MRG_PAK_DIR)
  message(FATAL_ERROR "assemble-pak: MRG_PAK_DIR is required")
endif()
if(NOT EXISTS "${MRG_EXE}")
  message(FATAL_ERROR "assemble-pak: executable not found: ${MRG_EXE}")
endif()

file(REMOVE_RECURSE "${MRG_PAK_DIR}")
file(MAKE_DIRECTORY "${MRG_PAK_DIR}")
file(COPY_FILE "${MRG_EXE}" "${MRG_PAK_DIR}/mrg-player")

# SDL_GetBasePath points at the executable, so the fonts travel next to it.
file(COPY "${MRG_SOURCE_DIR}/assets/" DESTINATION "${MRG_PAK_DIR}/res")

foreach(dir cores)
  if(IS_DIRECTORY "${MRG_SOURCE_DIR}/pack/${dir}")
    file(COPY "${MRG_SOURCE_DIR}/pack/${dir}/" DESTINATION "${MRG_PAK_DIR}/${dir}")
  endif()
endforeach()

foreach(file config.json icon.png launch.sh)
  if(EXISTS "${MRG_SOURCE_DIR}/pack/${file}")
    file(COPY_FILE "${MRG_SOURCE_DIR}/pack/${file}" "${MRG_PAK_DIR}/${file}")
  else()
    message(WARNING "assemble-pak: pack/${file} is missing from the source tree")
  endif()
endforeach()

if(MRG_STRIP AND EXISTS "${MRG_STRIP}")
  execute_process(COMMAND "${MRG_STRIP}" "${MRG_PAK_DIR}/mrg-player" RESULT_VARIABLE _strip_result)
  if(NOT _strip_result EQUAL 0)
    message(WARNING "assemble-pak: strip failed, shipping an unstripped binary")
  endif()
endif()

message(STATUS "pak assembled at ${MRG_PAK_DIR}")
