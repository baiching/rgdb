set(headers "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(storage)

add_executable(rgdb "rgdb.c")

target_link_libraries(rgdb PRIVATE storage)

# pg_query.h sits in lib/parser; PG_QUERY_INCLUDE_DIR comes from the top-level
# CMakeLists.txt.
target_include_directories(rgdb PRIVATE "${PG_QUERY_INCLUDE_DIR}")

# ---------------------------------------------------------------------------
# rgdb_headers : the target that publishes src/include to everyone else.
#
# An INTERFACE library is how an include directory is handed to others: link
# rgdb_headers (as rgdb does below) and the headers resolve out of the source
# tree during a build, while the INSTALL_INTERFACE expression makes them
# resolve from ${INSTALL_INCLUDE_DIR} once the project is installed.
# ---------------------------------------------------------------------------
add_library(rgdb_headers INTERFACE)

target_include_directories(rgdb_headers INTERFACE
  "$<BUILD_INTERFACE:${headers}>"
  "$<INSTALL_INTERFACE:${INSTALL_INCLUDE_DIR}>"
)

# Naming the headers keeps them visible to IDEs and ties them to the target.
target_sources(rgdb_headers INTERFACE
  "${headers}/common/malloc_mi.h"
  "${headers}/common/windows.h"
  "${headers}/data_structures/safe_map.h"
  "${headers}/storage/page_allocator.h"
  "${headers}/storage/bplustree/bplustree.h"
)

target_link_libraries(rgdb PRIVATE rgdb_headers)

# The trailing slash installs the *contents* of src/include, so a consumer's
# "storage/page_allocator.h" lands at
# ${INSTALL_INCLUDE_DIR}/storage/page_allocator.h.
install(
  DIRECTORY "${headers}/"
  DESTINATION "${INSTALL_INCLUDE_DIR}"
  FILES_MATCHING
  PATTERN "*.h")