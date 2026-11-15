cmake_minimum_required(VERSION 3.20)
project(MarkdownNativeEditor VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /wd4819)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test Svg)

# cmark-gfm settings
set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/cmark-gfm)

set(CORE_SOURCES
    src/theme/Theme.h
    src/theme/Theme.cpp

    src/core/Position.h
    src/core/Position.cpp
    src/core/TextBuffer.h
    src/core/TextBuffer.cpp
    src/core/EditTransaction.h
    src/core/EditTransaction.cpp
    src/core/UndoStack.h
    src/core/UndoStack.cpp

    src/parser/SemanticNode.h
    src/parser/MarkdownParserAdapter.h
    src/parser/MarkdownParserAdapter.cpp

    src/mapping/DisplayProjection.h
    src/mapping/DisplayProjection.cpp

    src/editor/LayoutEngine.h
    src/editor/LayoutEngine.cpp
    src/editor/NativeEditorView.h
    src/editor/NativeEditorView.cpp

    src/render/MathSvgRenderer.h
    src/render/MathSvgRenderer.cpp
    src/render/MermaidSvgRenderer.h
    src/render/MermaidSvgRenderer.cpp
    src/render/AsyncRenderService.h
    src/render/AsyncRenderService.cpp

    src/file/FileService.h
    src/file/FileService.cpp
    src/file/DocumentSession.h

    src/ui/OutlineWidget.h
    src/ui/OutlineWidget.cpp
    src/ui/SidebarWidget.h
    src/ui/SidebarWidget.cpp
    src/ui/SlashCommandPopup.h
    src/ui/SlashCommandPopup.cpp
    src/ui/FindReplaceBar.h
    src/ui/FindReplaceBar.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

add_library(markdown_editor_core STATIC ${CORE_SOURCES})

target_include_directories(markdown_editor_core PUBLIC
    src
    3rdparty/cmark-gfm/src
    3rdparty/cmark-gfm/extensions
    ${CMAKE_CURRENT_BINARY_DIR}/3rdparty/cmark-gfm/src
    ${CMAKE_CURRENT_BINARY_DIR}/3rdparty/cmark-gfm/extensions
)

target_link_libraries(markdown_editor_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    libcmark-gfm_static
    libcmark-gfm-extensions_static
)

# Main GUI executable
add_executable(MarkdownNativeEditor WIN32 src/main.cpp)
target_link_libraries(MarkdownNativeEditor PRIVATE markdown_editor_core)
set_property(SOURCE src/main.cpp PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MainWindow.h)

# Automated Test Suite for P0 Probes
enable_testing()
add_executable(test_probes tests/TestProbes.cpp)
target_link_libraries(test_probes PRIVATE markdown_editor_core Qt6::Test)
add_test(NAME TestProbes COMMAND test_probes -v1)
