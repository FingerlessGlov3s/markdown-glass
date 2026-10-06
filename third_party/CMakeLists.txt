# cmark-gfm: GitHub's markdown parser, pinned as a git submodule and linked statically.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)

add_subdirectory(cmark-gfm EXCLUDE_FROM_ALL)

add_library(cmark-gfm-bundled INTERFACE)
target_link_libraries(cmark-gfm-bundled INTERFACE
    libcmark-gfm-extensions_static libcmark-gfm_static)
target_include_directories(cmark-gfm-bundled SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmark-gfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/cmark-gfm/extensions
    ${CMAKE_CURRENT_BINARY_DIR}/cmark-gfm/src
    ${CMAKE_CURRENT_BINARY_DIR}/cmark-gfm/extensions)
target_compile_definitions(cmark-gfm-bundled INTERFACE
    CMARK_GFM_STATIC_DEFINE CMARK_GFM_EXTENSIONS_STATIC_DEFINE)
