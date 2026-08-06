# CMakeList.txt : CMake project for rgdb, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("rgdb")

set(PG_QUERY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/parser")
set(PG_QUERY_INCLUDE_DIR "${PG_QUERY_DIR}")

set(PG_QUERY_LIBRARY "${PG_QUERY_DIR}/pg_query.lib")

# Add source to this project's executable.
add_executable (rgdb "src/rgdb.cpp" "src/rgdb.h" "src/storage/bplustree/bplustree.cpp" "src/storage/page_allocator.cpp" "src/data_structures/safe_map.cpp")

target_include_directories(rgdb PRIVATE ${PG_QUERY_INCLUDE_DIR})
target_include_directories(rgdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/include)
target_include_directories(rgdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(rgdb PRIVATE ${PG_QUERY_LIBRARY})


if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET rgdb PROPERTY CXX_STANDARD 20)
endif()
target_compile_features(rgdb PRIVATE cxx_std_20)

# TODO: Add tests and install targets if needed.
# mimalloc 
include(FetchContent)

FetchContent_Declare(
  mimalloc
  GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
  GIT_TAG        v3.4.3 # Use the latest stable version
)
FetchContent_MakeAvailable(mimalloc)

# Link directly to the fetched target (it handles overrides automatically)
target_link_libraries(rgdb PRIVATE mimalloc)

