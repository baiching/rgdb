add_library(storage STATIC page_allocator.c)

# Sources include the headers qualified by module folder ("storage/..."), so
# the include root is src/include, not this directory.
target_include_directories(storage PUBLIC "${CMAKE_SOURCE_DIR}/src/include")
