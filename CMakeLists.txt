cmake_minimum_required(VERSION 3.20)
project(mrg_player VERSION 1.0 LANGUAGES CXX)

# The About page shows this string. A release build passes the tag it was cut
# from, so a packaged binary reports the version it actually is, while a plain
# checkout keeps reporting the project version.
set(MRG_VERSION_STRING "" CACHE STRING "Version reported by the launcher")
if(MRG_VERSION_STRING STREQUAL "")
  set(MRG_VERSION_STRING "${PROJECT_VERSION}")
endif()

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
# Screenshots are written as PNG when zlib is available; the capture code falls
# back to BMP otherwise.
find_package(ZLIB QUIET)

# The scripted --smoke-test-* runs drive the UI for CI. They are a developer
# tool, so release builds leave them out and reject the flags.
option(MRG_ENABLE_SMOKE_TESTS "Build the scripted --smoke-test-* command line" OFF)

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

add_library(mrg-platform STATIC
  src/config/AppPaths.cpp
  src/config/InputBindings.cpp
  src/game/GamePlatform.cpp
  src/util/ImageFile.cpp
  src/util/Utf8Path.cpp
)
target_include_directories(mrg-platform PUBLIC src)
target_compile_features(mrg-platform PUBLIC cxx_std_17)
target_compile_options(mrg-platform PRIVATE ${MRG_WARNING_FLAGS})
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
  src/util/Sha256.cpp
)
target_include_directories(runtime-libretro PUBLIC src third_party/libretro)
target_compile_features(runtime-libretro PUBLIC cxx_std_17)
target_compile_options(runtime-libretro PRIVATE ${MRG_WARNING_FLAGS})
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
  src/config/Settings.cpp
  src/i18n/Localization.cpp
  src/library/GameLibrary.cpp
  src/runtime/RuntimeRegistry.cpp
  src/model/Game.cpp
  src/ui/GameManagementScreen.cpp
  src/platform/RomFilePicker.cpp
  src/ui/HomeScreen.cpp
  src/ui/PauseMenuScreen.cpp
  src/ui/Primitives.cpp
  src/ui/SettingsScreen.cpp
  src/util/PngWriter.cpp
)
target_include_directories(mrg-player PRIVATE src)
target_compile_features(mrg-player PRIVATE cxx_std_17)
target_compile_options(mrg-player PRIVATE ${MRG_WARNING_FLAGS})
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

  add_executable(input-bindings-test tests/InputBindingsTest.cpp)
  target_include_directories(input-bindings-test PRIVATE src)
  target_compile_features(input-bindings-test PRIVATE cxx_std_17)
  target_compile_options(input-bindings-test PRIVATE ${MRG_WARNING_FLAGS})
  target_link_libraries(input-bindings-test PRIVATE mrg-platform)
  add_test(NAME input-bindings COMMAND input-bindings-test)

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
    src/ui/Primitives.cpp
    src/ui/PauseMenuScreen.cpp
    src/ui/GameManagementScreen.cpp
    src/ui/SettingsScreen.cpp
    src/model/Game.cpp
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
