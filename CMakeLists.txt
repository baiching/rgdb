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

set(3rdParty_lib "${CMAKE_SOURCE_DIR}/lib/data_structure")

#add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lib/data_structure")
add_subdirectory(src)

target_link_libraries(rgdb PRIVATE "${PG_QUERY_LIBRARY}" "${3rdParty_lib}")

# C11, and nothing else.
set_target_properties(rgdb PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF          # -std=c11 / /std:c11, not gnu11
)
target_compile_features(rgdb PRIVATE c_std_11)

if(MSVC)
  target_compile_options(rgdb PRIVATE /W4 /std:c11 /experimental:c11atomics)
  target_compile_definitions(rgdb PRIVATE
    _CRT_SECURE_NO_WARNINGS
  )
else()
  target_compile_options(rgdb PRIVATE -Wall -Wextra -Wpedantic -std=c11)
endif()


include(FetchContent)


  FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v3.5.3
  )
  FetchContent_MakeAvailable(mimalloc)

target_link_libraries(rgdb PRIVATE mimalloc)

