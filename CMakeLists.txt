# CMakeLists.txt : CMake project for rgdb, include source and define
# project specific logic here.
#
# This project is C11: every source file under src/ is C, and CMake is told the
# project's language is C. GoogleTest is C++, so it is confined to the optional
# `tests` target (RGDB_BUILD_GTEST=OFF by default).
#
# Caveat on the final link: mimalloc is force-compiled as C++ by its own
# CMakeLists on MSVC (see the mimalloc section below), so using the default
# RGDB_USE_MIMALLOC=ON pulls MSVCP140.dll into rgdb.exe. Use
# -DRGDB_USE_MIMALLOC=OFF for a binary that links only the C runtime.

cmake_minimum_required(VERSION 3.16)

# Enable Hot Reload for MSVC compilers if supported.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

# C is the primary language; CXX is enabled only to build GoogleTest, which is
# itself C++. The rgdb executable target never links the C++ runtime.
project(rgdb LANGUAGES C)

option(RGDB_BUILD_TESTS "Build the rgdb test executables" ON)
option(RGDB_USE_MIMALLOC "Allocate through mimalloc instead of the CRT heap" ON)

set(PG_QUERY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/parser")
set(PG_QUERY_INCLUDE_DIR "${PG_QUERY_DIR}")
set(PG_QUERY_LIBRARY "${PG_QUERY_DIR}/pg_query.lib")

# ---------------------------------------------------------------------------
# rgdb
# ---------------------------------------------------------------------------

add_executable(rgdb
  "src/rgdb.c"
  "src/rgdb.h"
  "src/storage/bplustree/bplustree.c"
  "src/storage/page_allocator.c"
  "src/data_structures/safe_map.c"
)

target_include_directories(rgdb PRIVATE
  "${PG_QUERY_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)

target_link_libraries(rgdb PRIVATE "${PG_QUERY_LIBRARY}")

# C11, and nothing else.
set_target_properties(rgdb PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF          # -std=c11 / /std:c11, not gnu11
)
target_compile_features(rgdb PRIVATE c_std_11)

if(MSVC)
  # /std:c11 for C11, /W4 for a clean warning level.
  #
  # /experimental:c11atomics is REQUIRED, not optional: MSVC defines
  # __STDC_NO_ATOMICS__ without it, and <stdatomic.h> then hits
  # `#error "C atomic support is not enabled"`. The page allocator depends on
  # <stdatomic.h> for its bump pointer and per-page state, so the build fails
  # outright without this switch. (Verified against vcruntime_c11_stdatomic.h.)
  #
  # _CRT_SECURE_NO_WARNINGS silences the strcpy/sscanf "unsafe" deprecations.
  #
  # Note: _CRTDBG_MAP_ALLOC is deliberately NOT defined. It makes <crtdbg.h>
  # redefine malloc/calloc/realloc/free/strdup, which collides with the
  # mimalloc overrides in common/malloc_mi.h (C4005) and silently wins,
  # defeating the allocator. mimalloc has its own debug checking via MI_DEBUG.
  target_compile_options(rgdb PRIVATE /W4 /std:c11 /experimental:c11atomics)
  target_compile_definitions(rgdb PRIVATE
    _CRT_SECURE_NO_WARNINGS
  )
else()
  target_compile_options(rgdb PRIVATE -Wall -Wextra -Wpedantic -std=c11)
endif()

# ---------------------------------------------------------------------------
# mimalloc
#
# mimalloc is a C library, so it participates in a C11 build without pulling in
# C++. It is fetched rather than vendored so the version stays pinned.
# ---------------------------------------------------------------------------

if(RGDB_USE_MIMALLOC)
  include(FetchContent)

  # These must be set BEFORE MakeAvailable: mimalloc's own CMakeLists reads
  # them at add_subdirectory time, so setting them afterwards has no effect.
  set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
  # Build only the static library. The shared build produces mimalloc-debug.dll
  # in a subdirectory that is not on the executable's DLL search path, so the
  # binaries fail at startup with 0xC0000135 (STATUS_DLL_NOT_FOUND).
  set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)

  # NOTE ON C++ PURITY (MSVC): MI_USE_CXX cannot be forced OFF on MSVC.
  # mimalloc's CMakeLists sets it unconditionally for MSVC/Intel/clang-cl
  # because it needs C11 atomics that MSVC's C mode does not provide, and it
  # calls enable_language(CXX) itself. So mimalloc's sources always compile as
  # C++ on Windows, and the resulting static library pulls MSVCP140.dll into
  # anything that links it -- including rgdb.exe.
  #
  # Consequence: with RGDB_USE_MIMALLOC=ON (the default) our *sources* are pure
  # C11 but the final link still carries the C++ runtime, inherited from the
  # dependency. Build with -DRGDB_USE_MIMALLOC=OFF for a binary that depends on
  # the C runtime only (verified: no MSVCP140D.dll in the import table).

  FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v3.4.3
  )
  FetchContent_MakeAvailable(mimalloc)

  # Link the static target when available (it handles overrides automatically).
  if(TARGET mimalloc-static)
    target_link_libraries(rgdb PRIVATE mimalloc-static)
  else()
    target_link_libraries(rgdb PRIVATE mimalloc)
  endif()

  target_compile_definitions(rgdb PRIVATE RGDB_USE_MIMALLOC=1)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------

if(RGDB_BUILD_TESTS)
  enable_testing()

  # --- Pure C11 tests, always available -----------------------------------
  add_executable(c11_tests
    "test/test_c11_modules.c"
    "src/storage/bplustree/bplustree.c"
    "src/storage/page_allocator.c"
    "src/data_structures/safe_map.c"
  )

  target_include_directories(c11_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib"
  )

  set_target_properties(c11_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
  )
  target_compile_features(c11_tests PRIVATE c_std_11)

  if(MSVC)
    target_compile_options(c11_tests PRIVATE /W4 /std:c11 /experimental:c11atomics)
    target_compile_definitions(c11_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(c11_tests PRIVATE -Wall -Wextra -Wpedantic -std=c11)
  endif()

  if(RGDB_USE_MIMALLOC)
    if(TARGET mimalloc-static)
      target_link_libraries(c11_tests PRIVATE mimalloc-static)
    else()
      target_link_libraries(c11_tests PRIVATE mimalloc)
    endif()
  endif()

  add_test(NAME c11_tests COMMAND c11_tests)

  # --- GoogleTest suite ---------------------------------------------------
  # GoogleTest is a C++ framework, so enabling this pulls CXX into the build.
  # It is deliberately a separate option so that a C-only build stays possible.
  option(RGDB_BUILD_GTEST "Build the GoogleTest-based test suite" OFF)

  if(RGDB_BUILD_GTEST)
    enable_language(CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)

    add_executable(tests
      "test/storage/test_page_allocator.cpp"
      "src/storage/page_allocator.c"
    )

    target_include_directories(tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    )

    target_compile_features(tests PRIVATE c_std_11)
    set_target_properties(tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

    if(MSVC)
      # This target mixes C and C++, so the C-only switches are gated on the
      # compile language; otherwise /std:c11 would be handed to cl.exe in C++
      # mode, where it is rejected.
      target_compile_options(tests PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:/std:c11;/experimental:c11atomics>"
      )
      target_compile_definitions(tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
      target_compile_options(tests PRIVATE "$<$<COMPILE_LANGUAGE:C>:-std=c11>")
    endif()

    target_link_libraries(tests PRIVATE GTest::gtest_main GTest::gmock_main Shlwapi.lib)

    if(RGDB_USE_MIMALLOC)
      if(TARGET mimalloc-static)
        target_link_libraries(tests PRIVATE mimalloc-static)
      else()
        target_link_libraries(tests PRIVATE mimalloc)
      endif()
    endif()

    gtest_discover_tests(tests)
  endif()
endif()
