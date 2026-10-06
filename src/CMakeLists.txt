# Parsing and the document model depend only on QtCore, so they can be fuzzed
# and tested without a GUI.
add_library(mdmodel STATIC
    model/converter.h
    model/document.cpp
    model/document.h
    model/htmlblockreader.cpp
    model/htmlsubset.cpp
    model/htmlsubset.h
    model/inlinebuilder.cpp
    model/inlinebuilder.h
    model/parser.cpp
    model/parser.h
    model/regularfile.cpp
    model/regularfile.h
    model/textcheck.cpp
    model/textcheck.h
)
target_include_directories(mdmodel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The parser library is an implementation detail: only model/ sees its headers.
target_link_libraries(mdmodel PUBLIC Qt6::Core PRIVATE cmark-gfm-bundled)
target_compile_options(mdmodel PRIVATE ${MDGLASS_WARNINGS})

# Everything else except main() lives in a static library so tests can link it.
# List every .cpp and .h here (alphabetically), so IDEs and tools see them all.
add_library(mdglass STATIC
    app/app.cpp
    app/app.h
    app/editor.cpp
    app/editor.h
    app/links.cpp
    app/links.h
    app/logging.cpp
    app/logging.h
    app/render.cpp
    app/render.h
    app/settings.cpp
    app/settings.h
    highlight/highlighter.cpp
    highlight/highlighter.h
    images/imageloader.cpp
    images/imageloader.h
    images/publicaddress.cpp
    images/publicaddress.h
    images/svgcheck.cpp
    images/svgcheck.h
    layout/imagesource.h
    layout/layout.cpp
    layout/layout.h
    layout/layoutbuilder.cpp
    layout/layoutinternal.h
    layout/layoutpaint.cpp
    layout/textbox.cpp
    layout/theme.cpp
    layout/theme.h
    print/printing.cpp
    print/printing.h
    resources.qrc
    ui/aboutdialog.cpp
    ui/aboutdialog.h
    ui/documentpane.cpp
    ui/documentpane.h
    ui/documentpane_file.cpp
    ui/documentpane_links.cpp
    ui/findbar.cpp
    ui/findbar.h
    ui/mainwindow.cpp
    ui/mainwindow.h
    ui/outlinedock.cpp
    ui/outlinedock.h
    ui/plaintext.cpp
    ui/plaintext.h
    ui/settingsdialog.cpp
    ui/settingsdialog.h
    view/documentview.cpp
    view/documentview.h
    view/documentview_input.cpp
    view/documentview_text.cpp
    view/limits.h
    view/minimap.cpp
    view/minimap.h
)
target_include_directories(mdglass PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdglass PUBLIC
    mdmodel
    Qt6::Widgets Qt6::PrintSupport Qt6::Network
    KF6::SyntaxHighlighting)
target_compile_options(mdglass PRIVATE ${MDGLASS_WARNINGS})

add_executable(markdown-glass app/main.cpp)
target_link_libraries(markdown-glass PRIVATE mdglass)
target_compile_options(markdown-glass PRIVATE ${MDGLASS_WARNINGS})
target_compile_definitions(markdown-glass PRIVATE MDGLASS_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS markdown-glass RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
