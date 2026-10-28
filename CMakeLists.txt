cmake_minimum_required(VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
#if(POLICY CMP0141)
#  cmake_policy(SET CMP0141 NEW)
#  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
#endif()


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(rgdb LANGUAGES C)


option(RGDB_BUILD_TESTS "Build the rgdb test executables" ON)
option(RGDB_USE_MIMALLOC "Allocate through mimalloc instead of the CRT heap" ON)

set(PG_QUERY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/parser")
set(PG_QUERY_INCLUDE_DIR "${PG_QUERY_DIR}")
set(PG_QUERY_LIBRARY "${PG_QUERY_DIR}/pg_query.lib")

set(3rdParty_lib "${CMAKE_SOURCE_DIR}/lib/data_structure")

add_subdirectory(src)

target_link_libraries(rgdb PRIVATE "${PG_QUERY_LIBRARY}" "${3rdParty_lib}")
target_include_directories(rgdb PRIVATE "${PG_QUERY_INCLUDE_DIR}")

# Let CMake handle the C standard for compilers that support it.
set_target_properties(rgdb PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(rgdb PRIVATE 
    /W4 
    /std:c11
    /experimental:c11atomics
  )
  target_compile_definitions(rgdb PRIVATE _CRT_SECURE_NO_WARNINGS)

 elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
  # Specific flags for clang-cl
  target_compile_options(rgdb PRIVATE
    -ffreestanding
    -fno-ms-compatibility   # <-- The key addition
    -std=c11               # Explicitly set the standard (clang-cl default is C17)
  )
else()
  target_compile_options(rgdb PRIVATE -Wall -Wextra -Wpedantic)
endif()

