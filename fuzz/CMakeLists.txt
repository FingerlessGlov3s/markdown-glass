foreach(target fuzz_html fuzz_markdown fuzz_textcheck)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE mdmodel)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endforeach()

# Untrusted-input checks from the GUI layers that need only QtCore (or
# QtNetwork), compiled in directly rather than linking the whole widget library.
set(fuzz_links_sources ${PROJECT_SOURCE_DIR}/src/app/links.cpp)
set(fuzz_svgcheck_sources ${PROJECT_SOURCE_DIR}/src/images/svgcheck.cpp)
set(fuzz_publicaddress_sources ${PROJECT_SOURCE_DIR}/src/images/publicaddress.cpp)
foreach(target fuzz_links fuzz_svgcheck fuzz_publicaddress)
    add_executable(${target} ${target}.cpp ${${target}_sources})
    target_link_libraries(${target} PRIVATE mdmodel)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endforeach()
target_link_libraries(fuzz_publicaddress PRIVATE Qt6::Network)
