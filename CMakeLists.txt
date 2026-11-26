cmake_minimum_required(VERSION 3.20)
project(mrg_player VERSION 1.0 LANGUAGES CXX)

# The About page shows this string. A release build passes the tag it was cut
# from, so a packaged binary reports the version it actually is, while a plain
# checkout keeps reporting the project version.
set(MRG_VERSION_STRING "" CACHE STRING "Version reported by the launcher")
if(MRG_VERSION_STRING STREQUAL "")
  set(MRG_VERSION_STRING "${PROJECT_VERSION}")
endif()

# The device build is the same program with a different display/input profile:
# one canvas size serves every platform, only the platform layer and the
# automatic filters (dialogs, downloads, scripted screenshots) differ.
set(MRG_PLATFORM "desktop" CACHE STRING "Target platform profile: desktop or brick")
set_property(CACHE MRG_PLATFORM PROPERTY STRINGS desktop brick)

option(MRG_USE_SYSTEM_SDL2 "Link the SDL2/SDL2_ttf installed on the target" OFF)
set(MRG_SDL2_ROOT "" CACHE PATH "Root of an SDL2 install (include/ and lib/)")
option(MRG_STRICT_WARNINGS "Add the extra warning set used while developing" OFF)
option(MRG_ENABLE_LTO "Link time optimisation for release builds" OFF)
option(MRG_ENABLE_ASAN "Build with AddressSanitizer and UBSan" OFF)
option(MRG_ENABLE_PAK_TARGET "Assemble the TrimUI .pak layout as a build target" OFF)
option(MRG_ENABLE_CORE_DOWNLOAD "Fetch libretro cores from the libretro buildbot at runtime" ON)

# The Brick Pro has no zenity, no curl and no offscreen SDL video driver, so the
# device profile turns those features off rather than discovering them at run
# time. Tests stay on the desktop because they need the dummy video driver.
if(MRG_PLATFORM STREQUAL "brick")
  set(MRG_USE_SYSTEM_SDL2 ON CACHE BOOL "" FORCE)
  set(MRG_ENABLE_SMOKE_TESTS OFF CACHE BOOL "" FORCE)
  set(MRG_ENABLE_PAK_TARGET ON CACHE BOOL "" FORCE)
  # The pak ships its cores and the firmware has no curl, so the device build
  # never grows the downloader in the first place.
  set(MRG_ENABLE_CORE_DOWNLOAD OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
elseif(NOT MRG_PLATFORM STREQUAL "desktop")
  message(FATAL_ERROR "MRG_PLATFORM must be desktop or brick, got ${MRG_PLATFORM}")
endif()

# Every screen draws on a canvas of logical pixels and the window is letterboxed
# around it. The canvas follows the panel instead of a fixed 16:9 size: a 4:3
# handheld draws its interface at 1024x768 and therefore never scales it by 0.8,
# which is what turned its text to mush. Only the size differs; the layouts are
# the same code. A preview build can ask for any other size.
set(MRG_CANVAS_WIDTH "" CACHE STRING "Canvas width in logical pixels (default: 1280 desktop, 1024 brick)")
set(MRG_CANVAS_HEIGHT "" CACHE STRING "Canvas height in logical pixels (default: 720 desktop, 768 brick)")
if(MRG_PLATFORM STREQUAL "brick")
  set(_mrg_default_canvas_width 1024)
  set(_mrg_default_canvas_height 768)
else()
  set(_mrg_default_canvas_width 1280)
  set(_mrg_default_canvas_height 720)
endif()
if(MRG_CANVAS_WIDTH STREQUAL "")
  set(_mrg_canvas_width ${_mrg_default_canvas_width})
else()
  set(_mrg_canvas_width ${MRG_CANVAS_WIDTH})
endif()
if(MRG_CANVAS_HEIGHT STREQUAL "")
  set(_mrg_canvas_height ${_mrg_default_canvas_height})
else()
  set(_mrg_canvas_height ${MRG_CANVAS_HEIGHT})
endif()
math(EXPR _mrg_canvas_width "${_mrg_canvas_width}")
math(EXPR _mrg_canvas_height "${_mrg_canvas_height}")
if(_mrg_canvas_width LESS 320 OR _mrg_canvas_height LESS 240 OR
   _mrg_canvas_width GREATER 8192 OR _mrg_canvas_height GREATER 8192)
  message(FATAL_ERROR "Canvas must be between 320x240 and 8192x8192, got ${_mrg_canvas_width}x${_mrg_canvas_height}")
endif()
message(STATUS "Canvas: ${_mrg_canvas_width}x${_mrg_canvas_height}")


# An unset CMAKE_BUILD_TYPE leaves every target on the compiler's default,
# which for GCC is -O0: the emulator then runs about five times slower than it
# needs to and a whole frame costs milliseconds instead of a fraction of one.
# Ship optimised unless the person building asks for something else.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected; defaulting to Release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# cl.exe rejects the GCC warning spellings and reads sources as the active
# code page unless told otherwise: the localisation table holds UTF-8 Chinese.
if(MSVC)
  set(MRG_WARNING_FLAGS /W4 /utf-8)
else()
  set(MRG_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

# CI builds the project's own sources with -Werror so a fresh warning cannot
# land unnoticed; SDL2 and its dependencies keep their own flags.
option(MRG_WARNINGS_AS_ERRORS "Treat warnings in MRG targets as errors" OFF)
if(MRG_WARNINGS_AS_ERRORS AND NOT MSVC)
  list(APPEND MRG_WARNING_FLAGS -Werror)
endif()
# Every target of this project gets its language level, warning set and
# platform defines from one place, so adding a target cannot silently skip the
# device profile. Bundled third-party trees keep their own flags.
function(mrg_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  target_compile_options(${target} PRIVATE ${MRG_WARNING_FLAGS})
  if(MRG_ENABLE_CORE_DOWNLOAD)
    target_compile_definitions(${target} PRIVATE MRG_ENABLE_CORE_DOWNLOAD=1)
  else()
    target_compile_definitions(${target} PRIVATE MRG_ENABLE_CORE_DOWNLOAD=0)
  endif()
  get_target_property(_mrg_type ${target} TYPE)
  if(MRG_PLATFORM STREQUAL "brick")
    # The firmware ships no libstdc++ of a known version, so it is linked in
    # statically; section GC keeps the stripped binary small.
    target_compile_definitions(${target} PRIVATE MRG_PLATFORM_BRICK=1)
    target_compile_options(${target} PRIVATE
      -ffunction-sections -fdata-sections -fvisibility=hidden)
    if(NOT _mrg_type STREQUAL "STATIC_LIBRARY")
      target_link_options(${target} PRIVATE
        -static-libstdc++ -static-libgcc
        -Wl,--gc-sections -Wl,--as-needed)
    endif()
    # GCC 8 still ships <filesystem> as a separate library; 9 merged it into
    # libstdc++, which is why the desktop build never needed this.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
      target_link_libraries(${target} PUBLIC stdc++fs)
    endif()
  endif()
  if(MRG_ENABLE_LTO AND NOT MRG_PLATFORM STREQUAL "brick")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _mrg_ipo LANGUAGES CXX)
    if(_mrg_ipo)
      set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
  endif()
  if(MRG_ENABLE_ASAN AND NOT MSVC)
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
  if(MRG_STRICT_WARNINGS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -Wshadow -Wconversion -Wold-style-cast)
  endif()
endfunction()

# Screenshots are written as PNG when zlib is available; the capture code falls
# back to BMP otherwise. Both SDL2 flavours need it.
find_package(ZLIB QUIET)

# CoverCache runs a worker thread. Recent glibc merged pthread into libc, the
# device runs one that did not, so the dependency is declared instead of being
# inherited by accident.
find_package(Threads REQUIRED)

if(MRG_USE_SYSTEM_SDL2)
  # The Brick Pro firmware ships SDL2 2.0.26 and SDL2_ttf 2.0.10 without CMake
  # package configuration, so the libraries are located directly and wrapped in
  # the same target names the vendored build produces.
  set(_mrg_sdl2_roots ${MRG_SDL2_ROOT} ${CMAKE_SYSROOT}/usr)
  find_path(MRG_SDL2_INCLUDE_DIR SDL.h
    PATHS ${_mrg_sdl2_roots} PATH_SUFFIXES include/SDL2 include NO_DEFAULT_PATH)
  find_library(MRG_SDL2_LIBRARY NAMES SDL2-2.0 SDL2
    PATHS ${_mrg_sdl2_roots} PATH_SUFFIXES lib NO_DEFAULT_PATH)
  find_library(MRG_SDL2TTF_LIBRARY NAMES SDL2_ttf-2.0 SDL2_ttf
    PATHS ${_mrg_sdl2_roots} PATH_SUFFIXES lib NO_DEFAULT_PATH)
  if(NOT MRG_SDL2_INCLUDE_DIR OR NOT MRG_SDL2_LIBRARY OR NOT MRG_SDL2TTF_LIBRARY)
    message(FATAL_ERROR "System SDL2/SDL2_ttf not found under: ${_mrg_sdl2_roots}."
      " Pass -DMRG_SDL2_ROOT=<prefix> if they live elsewhere.")
  endif()
  message(STATUS "Using system SDL2: ${MRG_SDL2_LIBRARY}")
  add_library(SDL2::SDL2 UNKNOWN IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION "${MRG_SDL2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MRG_SDL2_INCLUDE_DIR}")
  add_library(SDL2_ttf::SDL2_ttf UNKNOWN IMPORTED)
  set_target_properties(SDL2_ttf::SDL2_ttf PROPERTIES
    IMPORTED_LOCATION "${MRG_SDL2TTF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MRG_SDL2_INCLUDE_DIR}")
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)

  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/CMakeLists.txt" OR
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2_ttf/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 submodules are missing. Run: git submodule update --init --recursive")
  endif()

  # The vendored FreeType still asks for CMake 3.0 and SDL2 for 3.0.0, which
  # CMake 4 refuses outright ("Compatibility with CMake < 3.5 has been
  # removed"). Raising the policy floor for the bundled trees keeps CMake 4
  # builders, such as the GitHub runners, configuring; older CMake ignores the
  # variable. Our own targets keep the 3.20 policies declared above.
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.31)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  endif()

  add_subdirectory(third_party/SDL2 EXCLUDE_FROM_ALL)
  add_subdirectory(third_party/SDL2_ttf EXCLUDE_FROM_ALL)

  unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

# The canvas size is a property of the whole program, so every target that
# compiles a screen - the player and the rendering test alike - sees the same
# numbers. Third-party trees are added above this point and stay untouched.
add_compile_definitions(MRG_CANVAS_WIDTH=${_mrg_canvas_width}
                        MRG_CANVAS_HEIGHT=${_mrg_canvas_height})

add_library(mrg-platform STATIC
  src/config/AppPaths.cpp
  src/config/InputBindings.cpp
  src/game/GamePlatform.cpp
  src/util/ImageFile.cpp
  src/util/Utf8Path.cpp
)
target_include_directories(mrg-platform PUBLIC src)
mrg_configure_target(mrg-platform)
target_link_libraries(mrg-platform PUBLIC Threads::Threads)
if(TARGET SDL2::SDL2-static)
  target_link_libraries(mrg-platform PUBLIC SDL2::SDL2-static)
else()
  target_link_libraries(mrg-platform PUBLIC SDL2::SDL2)
endif()

# Games the built-in NES runtime does not cover play through installed libretro
# cores, which are discovered and dlopen-ed at runtime.
add_library(runtime-libretro STATIC
  src/runtime/libretro/CoreArchive.cpp
  src/runtime/libretro/CoreDiscovery.cpp
  src/runtime/libretro/CoreInstaller.cpp
  src/runtime/libretro/LibretroCore.cpp
  src/runtime/libretro/LibretroRuntime.cpp
  src/util/Download.cpp
  src/util/Sha256.cpp
)
target_include_directories(runtime-libretro PUBLIC src third_party/libretro)
mrg_configure_target(runtime-libretro)
target_link_libraries(runtime-libretro PUBLIC mrg-platform ${CMAKE_DL_LIBS})
# Downloaded cores ship as .so.zip, so unpacking needs zlib as well.
if(ZLIB_FOUND)
  target_compile_definitions(runtime-libretro PRIVATE MRG_HAVE_ZLIB=1)
  target_link_libraries(runtime-libretro PRIVATE ZLIB::ZLIB)
endif()

add_executable(mrg-player
  src/main.cpp
  src/app/Application.cpp
  src/app/LaunchOptions.cpp
  src/app/RenderProbe.cpp
  src/app/RenderSurface.cpp
  src/config/Settings.cpp
  src/i18n/CoreOptionText.cpp
  src/i18n/Localization.cpp
  src/library/CoverNames.cpp
  src/library/GameLibrary.cpp
  src/library/RomScanner.cpp
  src/runtime/RuntimeRegistry.cpp
  src/model/Game.cpp
  src/ui/Controls.cpp
  src/ui/CoverCache.cpp
  src/ui/GameManagementScreen.cpp
  src/platform/RomFilePicker.cpp
  src/ui/HomeScreen.cpp
  src/ui/PauseMenuScreen.cpp
  src/ui/Primitives.cpp
  src/ui/SettingsScreen.cpp
  src/util/Image.cpp
  src/util/PngWriter.cpp
)
target_include_directories(mrg-player PRIVATE src third_party/stb)
mrg_configure_target(mrg-player)
# The About page reports this string, so the release number lives in one place.
target_compile_definitions(mrg-player PRIVATE MRG_VERSION="${MRG_VERSION_STRING}")
target_link_libraries(mrg-player PRIVATE runtime-libretro)

if(MRG_ENABLE_SMOKE_TESTS)
  target_compile_definitions(mrg-player PRIVATE MRG_ENABLE_SMOKE_TESTS=1)
endif()

if(ZLIB_FOUND)
  target_compile_definitions(mrg-player PRIVATE MRG_HAVE_ZLIB=1)
  target_link_libraries(mrg-player PRIVATE ZLIB::ZLIB)
endif()

if(WIN32)
  # SDL2 ships neither an HTTP client nor a file dialog, so the installer
  # downloads over WinHTTP and the picker uses the Win32 common dialog. That
  # also keeps the launcher working on a stock Windows without curl.
  target_link_libraries(runtime-libretro PUBLIC winhttp)
  target_link_libraries(mrg-player PRIVATE comdlg32)
  foreach(target mrg-platform runtime-libretro mrg-player)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
  # SDL2 renames main to SDL_main on Windows and leaves the real entry point to
  # SDL2main. Test executables are console programs with their own main, so
  # every target below keeps the plain name; mrg-player still goes through
  # SDL2main.
  add_compile_definitions(SDL_MAIN_HANDLED)

  add_executable(game-library-test
    tests/GameLibraryTest.cpp
    src/config/Settings.cpp
    src/library/GameLibrary.cpp
    src/library/RomScanner.cpp
    src/runtime/RuntimeRegistry.cpp
  )
  target_include_directories(game-library-test PRIVATE src)
  target_compile_features(game-library-test PRIVATE cxx_std_17)
  target_compile_options(game-library-test PRIVATE ${MRG_WARNING_FLAGS})
  if(TARGET SDL2::SDL2-static)
    target_link_libraries(game-library-test PRIVATE runtime-libretro SDL2::SDL2-static)
  else()
    target_link_libraries(game-library-test PRIVATE runtime-libretro SDL2::SDL2)
  endif()
  add_test(NAME game-library COMMAND game-library-test)

  add_executable(sha256-test tests/Sha256Test.cpp src/util/Sha256.cpp
    src/util/Utf8Path.cpp)
  target_include_directories(sha256-test PRIVATE src)
  target_compile_features(sha256-test PRIVATE cxx_std_17)
  target_compile_options(sha256-test PRIVATE ${MRG_WARNING_FLAGS})
  add_test(NAME sha256 COMMAND sha256-test)

  add_executable(core-option-text-test tests/CoreOptionTextTest.cpp
    src/i18n/CoreOptionText.cpp)
  target_include_directories(core-option-text-test PRIVATE src)
  target_compile_features(core-option-text-test PRIVATE cxx_std_17)
  target_compile_options(core-option-text-test PRIVATE ${MRG_WARNING_FLAGS})
  add_test(NAME core-option-text COMMAND core-option-text-test)

  add_executable(input-bindings-test tests/InputBindingsTest.cpp)
  target_include_directories(input-bindings-test PRIVATE src)
  target_compile_features(input-bindings-test PRIVATE cxx_std_17)
  target_compile_options(input-bindings-test PRIVATE ${MRG_WARNING_FLAGS})
  target_link_libraries(input-bindings-test PRIVATE mrg-platform)
  add_test(NAME input-bindings COMMAND input-bindings-test)

  # Cover names and box art URLs are pure string work, so they can be checked
  # without the network or a renderer.
  add_executable(cover-names-test
    tests/CoverNamesTest.cpp
    src/library/CoverNames.cpp
  )
  target_include_directories(cover-names-test PRIVATE src)
  target_compile_features(cover-names-test PRIVATE cxx_std_17)
  target_compile_options(cover-names-test PRIVATE ${MRG_WARNING_FLAGS})
  target_link_libraries(cover-names-test PRIVATE mrg-platform)
  add_test(NAME cover-names COMMAND cover-names-test)

  # A stub libretro core stands in for a real one so the runtime can be tested
  # without shipping or downloading an emulator library. Multi-config
  # generators append a per-config directory unless the output path carries a
  # generator expression, and the tests look for cores/ directly under the
  # build tree, so $<0:> suppresses it.
  set(stub_core_dir "${CMAKE_CURRENT_BINARY_DIR}/cores/$<0:>")
  add_library(stub-libretro-core MODULE tests/libretro/StubCore.cpp)
  target_include_directories(stub-libretro-core PRIVATE third_party/libretro)
  target_compile_features(stub-libretro-core PRIVATE cxx_std_17)
  target_compile_options(stub-libretro-core PRIVATE ${MRG_WARNING_FLAGS})
  set_target_properties(stub-libretro-core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mgba_libretro"
    LIBRARY_OUTPUT_DIRECTORY "${stub_core_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${stub_core_dir}")

  # NES now goes through libretro as well, so the stub is also built under a NES
  # core name to prove that routing.
  add_library(stub-nes-core MODULE tests/libretro/StubCore.cpp)
  target_include_directories(stub-nes-core PRIVATE third_party/libretro)
  target_compile_features(stub-nes-core PRIVATE cxx_std_17)
  target_compile_options(stub-nes-core PRIVATE ${MRG_WARNING_FLAGS})
  set_target_properties(stub-nes-core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mesen_libretro"
    LIBRARY_OUTPUT_DIRECTORY "${stub_core_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${stub_core_dir}")
  if(WIN32)
    # The stub defines plain C entry points, so Windows needs an explicit export.
    set_target_properties(stub-libretro-core stub-nes-core PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
  if(APPLE)
    # CMake names modules *.so on macOS, but libretro cores are *.dylib there.
    set_target_properties(stub-libretro-core stub-nes-core PROPERTIES SUFFIX ".dylib")
  endif()

  add_executable(libretro-runtime-test
    tests/LibretroRuntimeTest.cpp
    src/runtime/RuntimeRegistry.cpp
  )
  target_include_directories(libretro-runtime-test PRIVATE src)
  target_compile_features(libretro-runtime-test PRIVATE cxx_std_17)
  target_compile_options(libretro-runtime-test PRIVATE ${MRG_WARNING_FLAGS})
  target_link_libraries(libretro-runtime-test PRIVATE runtime-libretro)
  if(ZLIB_FOUND)
    target_compile_definitions(libretro-runtime-test PRIVATE MRG_HAVE_ZLIB=1)
    target_link_libraries(libretro-runtime-test PRIVATE ZLIB::ZLIB)
  endif()
  add_dependencies(libretro-runtime-test stub-libretro-core stub-nes-core)
  add_test(NAME libretro-runtime COMMAND libretro-runtime-test)
  set_tests_properties(libretro-runtime PROPERTIES ENVIRONMENT
    "MRG_CORES_DIR=${CMAKE_CURRENT_BINARY_DIR}/cores;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

if(TARGET SDL2::SDL2-static)
  target_link_libraries(mrg-player PRIVATE SDL2::SDL2-static)
else()
  target_link_libraries(mrg-player PRIVATE SDL2::SDL2)
endif()

if(TARGET SDL2_ttf::SDL2_ttf-static)
  target_link_libraries(mrg-player PRIVATE SDL2_ttf::SDL2_ttf-static)
else()
  target_link_libraries(mrg-player PRIVATE SDL2_ttf::SDL2_ttf)
endif()

if(MRG_PLATFORM STREQUAL "brick")
  # The cross toolchain carries glibc 2.28 while the device does not, and ld
  # picks the 2.28 libm up through the SDL2 dependency before the sysroot copy,
  # which would make powf a GLIBC_2.27 symbol. Naming the 2.23 libm from the
  # sysroot pins it back. Only that one file: adding the whole usr/lib to the
  # rpath-link path mixes in a libpthread that fails with
  # "__libc_dl_error_tsd@GLIBC_PRIVATE".
  if(EXISTS "${CMAKE_SYSROOT}/usr/lib/libm.so.6")
    target_link_libraries(mrg-player PRIVATE "${CMAKE_SYSROOT}/usr/lib/libm.so.6")
  endif()
endif()

# Multi-config generators place the executable in a per-config directory, so the
# font has to sit beside it rather than only in the top of the build tree. A
# macOS bundle is a directory, and SDL_GetBasePath reports its Resources folder
# there, so the assets go inside the bundle instead of next to the binary.
if(APPLE)
  add_custom_command(TARGET mrg-player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "$<TARGET_BUNDLE_CONTENT_DIR:mrg-player>/Resources/assets"
    COMMENT "Copying assets into the application bundle")
else()
  add_custom_command(TARGET mrg-player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "$<TARGET_FILE_DIR:mrg-player>/assets"
    COMMENT "Copying assets next to the executable")
endif()

if(BUILD_TESTING)
  add_executable(ui-rendering-test
    tests/UiRenderingTest.cpp
    src/ui/Controls.cpp
    src/ui/Primitives.cpp
    src/ui/HomeScreen.cpp
    src/ui/PauseMenuScreen.cpp
    src/ui/GameManagementScreen.cpp
    src/ui/SettingsScreen.cpp
    src/model/Game.cpp
    src/library/GameLibrary.cpp
    src/i18n/CoreOptionText.cpp
    src/i18n/Localization.cpp
  )
  target_include_directories(ui-rendering-test PRIVATE src)
  target_compile_features(ui-rendering-test PRIVATE cxx_std_17)
  target_link_libraries(ui-rendering-test PRIVATE mrg-platform)
  if(TARGET SDL2_ttf::SDL2_ttf-static)
    target_link_libraries(ui-rendering-test PRIVATE SDL2_ttf::SDL2_ttf-static)
  else()
    target_link_libraries(ui-rendering-test PRIVATE SDL2_ttf::SDL2_ttf)
  endif()
  add_test(NAME ui-rendering COMMAND ui-rendering-test "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DroidSansFallbackFull.ttf")
  set_tests_properties(ui-rendering PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

  if(ZLIB_FOUND)
    add_executable(png-writer-test tests/PngWriterTest.cpp src/util/PngWriter.cpp
      src/util/Utf8Path.cpp)
    target_include_directories(png-writer-test PRIVATE src)
    target_compile_features(png-writer-test PRIVATE cxx_std_17)
    target_compile_options(png-writer-test PRIVATE ${MRG_WARNING_FLAGS})
    target_compile_definitions(png-writer-test PRIVATE MRG_HAVE_ZLIB=1)
    target_link_libraries(png-writer-test PRIVATE ZLIB::ZLIB)
    add_test(NAME png-writer COMMAND png-writer-test)
  endif()

  add_executable(video-viewport-test tests/VideoViewportTest.cpp)
  target_include_directories(video-viewport-test PRIVATE src)
  target_compile_features(video-viewport-test PRIVATE cxx_std_17)
  target_compile_options(video-viewport-test PRIVATE ${MRG_WARNING_FLAGS})
  add_test(NAME video-viewport COMMAND video-viewport-test)

  add_executable(render-surface-test tests/RenderSurfaceTest.cpp
    src/app/RenderSurface.cpp)
  target_include_directories(render-surface-test PRIVATE src)
  target_compile_features(render-surface-test PRIVATE cxx_std_17)
  target_compile_options(render-surface-test PRIVATE ${MRG_WARNING_FLAGS})
  add_test(NAME render-surface COMMAND render-surface-test)
endif()

file(COPY assets/fonts DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets")

if(TARGET SDL2::SDL2main)
  target_link_libraries(mrg-player PRIVATE SDL2::SDL2main)
endif()

if(APPLE)
  # A macOS release is a bundle so Finder treats it as an application; the
  # fonts travel inside it, which is where SDL_GetBasePath looks first.
  set_target_properties(mrg-player PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "MRG Player"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.laomou.mrg-player"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MRG_VERSION_STRING}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${MRG_VERSION_STRING}")
  install(TARGETS mrg-player BUNDLE DESTINATION . RUNTIME DESTINATION bin)
else()
  install(TARGETS mrg-player RUNTIME DESTINATION bin)
endif()
install(DIRECTORY assets/fonts DESTINATION share/mrg-player)
# The pak target lays out exactly what goes on the SD card, so packaging is a
# build step rather than a wiki page, and pak-verify checks that layout against
# the firmware the device actually runs.
if(MRG_ENABLE_PAK_TARGET)
  set(MRG_PAK_DIR "${CMAKE_BINARY_DIR}/pak/MRG.pak" CACHE PATH "Assembled pak directory")

  set(_mrg_readelf "${CMAKE_READELF}")
  if(NOT _mrg_readelf)
    set(_mrg_readelf readelf)
  endif()
  set(_mrg_objdump "${CMAKE_OBJDUMP}")
  if(NOT _mrg_objdump)
    set(_mrg_objdump objdump)
  endif()

  add_custom_target(pak
    COMMAND ${CMAKE_COMMAND}
      -DMRG_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -DMRG_PAK_DIR=${MRG_PAK_DIR}
      -DMRG_EXE=$<TARGET_FILE:mrg-player>
      -DMRG_STRIP=${CMAKE_STRIP}
      -P ${CMAKE_SOURCE_DIR}/cmake/assemble-pak.cmake
    DEPENDS mrg-player
    COMMENT "Assembling MRG.pak"
    VERBATIM)

  add_custom_target(pak-verify
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/verify-pak.sh
      ${MRG_PAK_DIR} ${_mrg_readelf} ${_mrg_objdump}
    DEPENDS pak
    COMMENT "Verifying MRG.pak against the device runtime"
    VERBATIM)
endif()
