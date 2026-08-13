cmake_minimum_required(VERSION 3.16)
project(ensemble VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Determine Build Type (Debug vs Release)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Check Git dirty / untracked status
find_package(Git QUIET)
set(ENSEMBLE_GIT_DIRTY "")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_STATUS_OUTPUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT GIT_STATUS_OUTPUT STREQUAL "")
    set(ENSEMBLE_GIT_DIRTY "-dirty")
  endif()
endif()

set(ENSEMBLE_FULL_VERSION "${PROJECT_VERSION} (${CMAKE_BUILD_TYPE}${ENSEMBLE_GIT_DIRTY})")

add_compile_definitions(
  ENSEMBLE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
  ENSEMBLE_FULL_VERSION="${ENSEMBLE_FULL_VERSION}"
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/EditorPane.cpp
    src/ui/HtmlSourcePane.cpp
    src/ui/CssEditorPane.cpp
    src/ui/PreviewPane.cpp
    src/ui/ChapterSidebar.cpp
    src/ui/CodeEditor.cpp
    src/ui/HtmlSyntaxHighlighter.cpp
    src/ui/CssSyntaxHighlighter.cpp
    src/model/Chapter.cpp
    src/model/Ao3Project.cpp
    src/model/ProjectSerializer.cpp
    src/model/ZstdArchive.cpp
    src/export/Ao3HtmlExporter.cpp
    src/export/Ao3HtmlSanitizer.cpp
    src/export/Ao3HtmlImporter.cpp
    src/ui/MainMenuWindow.cpp
    src/ui/AboutDialog.cpp
    src/ui/AppIcon.cpp
    src/ui/FindReplaceDialog.cpp
    src/ui/EditorMinimap.cpp
    src/checker/SpellChecker.cpp
    src/net/Ao3Parser.cpp
    src/net/Ao3Session.cpp
    src/net/Ao3Client.cpp
    src/net/Ao3AuthServer.cpp
    src/ui/Ao3ImportDialog.cpp
    src/ui/Ao3SkinDiffDialog.cpp
)

set(HEADERS
    src/ui/MainWindow.h
    src/ui/EditorPane.h
    src/ui/HtmlSourcePane.h
    src/ui/CssEditorPane.h
    src/ui/PreviewPane.h
    src/ui/ChapterSidebar.h
    src/ui/CodeEditor.h
    src/ui/HtmlSyntaxHighlighter.h
    src/ui/CssSyntaxHighlighter.h
    src/ui/EditorMinimap.h
    src/ui/FindReplaceDialog.h
    src/checker/SpellChecker.h
    src/model/Chapter.h
    src/model/Ao3Project.h
    src/model/ProjectSerializer.h
    src/model/ZstdArchive.h
    src/export/Ao3TagWhitelist.h
    src/export/Ao3HtmlExporter.h
    src/export/Ao3HtmlSanitizer.h
    src/export/Ao3HtmlImporter.h
    src/ui/MainMenuWindow.h
    src/ui/AboutDialog.h
    src/ui/AppIcon.h
    src/net/Ao3Types.h
    src/net/Ao3Parser.h
    src/net/Ao3Session.h
    src/net/Ao3Client.h
    src/net/Ao3AuthServer.h
    src/ui/Ao3ImportDialog.h
    src/ui/Ao3SkinDiffDialog.h
)

qt_add_executable(ensemble ${SOURCES} ${HEADERS})

target_include_directories(ensemble PRIVATE src)

target_link_libraries(ensemble PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)

qt_add_resources(ensemble resources/resources.qrc)

install(TARGETS ensemble RUNTIME DESTINATION bin)

# Unit Tests
add_executable(test_ao3_parser test/cpp/test_ao3_parser.cpp src/net/Ao3Parser.cpp)
target_include_directories(test_ao3_parser PRIVATE src)
target_link_libraries(test_ao3_parser PRIVATE Qt6::Core Qt6::Gui)
