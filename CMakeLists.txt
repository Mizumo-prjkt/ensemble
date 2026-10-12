cmake_minimum_required(VERSION 3.16)
project(ensemble VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Determine Build Type (Debug vs Release) for single-config generators
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Version overrides & codename localversion
# e.g. -DENSEMBLE_VERSION_OVERRIDE="1.2.0" -DENSEMBLE_LOCALVERSION="agnestachyon" -> "1.2.0-agnestachyon"
if(ENSEMBLE_VERSION_OVERRIDE)
  set(ENSEMBLE_BASE_VERSION "${ENSEMBLE_VERSION_OVERRIDE}")
else()
  set(ENSEMBLE_BASE_VERSION "${PROJECT_VERSION}")
endif()

if(ENSEMBLE_LOCALVERSION)
  set(ENSEMBLE_FULL_VERSION "${ENSEMBLE_BASE_VERSION}-${ENSEMBLE_LOCALVERSION}")
else()
  set(ENSEMBLE_FULL_VERSION "${ENSEMBLE_BASE_VERSION}")
endif()

# Custom output binary directory
if(ENSEMBLE_OUTPUT_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENSEMBLE_OUTPUT_DIR}")
endif()

# Extra C/C++ compiler flags override
if(ENSEMBLE_EXTRA_CXX_FLAGS)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ENSEMBLE_EXTRA_CXX_FLAGS}")
endif()
if(ENSEMBLE_EXTRA_C_FLAGS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ENSEMBLE_EXTRA_C_FLAGS}")
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

set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG "RELEASE" "DEBUG" "")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "RELEASE" "DEBUG" "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RELEASE" "DEBUG" "")
set(QT_NO_ADD_WINDOWS_MANIFEST TRUE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets WebSockets)
find_package(Qt6 QUIET COMPONENTS WebEngineQuick)
find_package(ZLIB QUIET)
if(NOT TARGET ZLIB::ZLIB)
    include(FetchContent)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
    )
    FetchContent_MakeAvailable(zlib)
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
endif()

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
    src/model/AgenticData.cpp
    src/model/LoreGraph.cpp
    src/model/LoreManager.cpp
    src/model/ProjectSerializer.cpp
    src/model/ZstdArchive.cpp
    src/utils/AesCipher.cpp
    src/export/Ao3HtmlExporter.cpp
    src/export/Ao3HtmlSanitizer.cpp
    src/export/Ao3HtmlImporter.cpp
    src/ui/MainMenuWindow.cpp
    src/ui/AboutDialog.cpp
    src/ui/CreditsDialog.cpp
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
    src/ui/DebugConsoleDialog.cpp
    src/ui/ProblemsDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/HelpGuideDialog.cpp
    src/ui/AgentThoughtsDialog.cpp
    src/ui/LoreGraphDialog.cpp
    src/ui/ProjectPropertiesDialog.cpp
    src/ui/ProjectLoadErrorDialog.cpp
    src/ui/ImageEmbedDialog.cpp
    src/themes/themes.cpp
    src/debug/debug.cpp
    src/utils/AppPaths.cpp
    src/utils/miniz.cpp
    src/utils/SingleInstanceManager.cpp
    src/mcp/McpServer.cpp
    src/mcp/McpHandler.cpp
    src/mcp/McpAgentInstaller.cpp
    src/mcp/McpBridge.cpp
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
    src/model/AgenticData.h
    src/model/LoreGraph.h
    src/model/LoreManager.h
    src/model/ProjectSerializer.h
    src/model/ZstdArchive.h
    src/utils/AesCipher.h
    src/export/Ao3TagWhitelist.h
    src/export/Ao3HtmlExporter.h
    src/export/Ao3HtmlSanitizer.h
    src/export/Ao3HtmlImporter.h
    src/ui/MainMenuWindow.h
    src/ui/AboutDialog.h
    src/ui/CreditsDialog.h
    src/ui/ProjectLoadErrorDialog.h
    src/ui/ImageEmbedDialog.h
    src/ui/AppIcon.h
    src/net/Ao3Types.h
    src/net/Ao3Parser.h
    src/net/Ao3Session.h
    src/net/Ao3Client.h
    src/net/Ao3AuthServer.h
    src/ui/Ao3ImportDialog.h
    src/ui/Ao3SkinDiffDialog.h
    src/ui/DebugConsoleDialog.h
    src/ui/ProblemsDialog.h
    src/ui/SettingsDialog.h
    src/ui/HelpGuideDialog.h
    src/ui/AgentThoughtsDialog.h
    src/ui/LoreGraphDialog.h
    src/ui/ProjectPropertiesDialog.h
    src/themes/ThemeManager.h
    src/themes/themes.h
    src/debug/debug.hpp
    src/utils/AppPaths.h
    src/utils/miniz.h
    src/utils/SingleInstanceManager.h
    src/mcp/McpServer.h
    src/mcp/McpHandler.h
    src/mcp/AgentActivity.h
    src/mcp/McpAgentInstaller.h
    src/mcp/McpBridge.h
)

set(CMAKE_QT_NO_ADD_WINDOWS_MANIFEST TRUE)
set(QT_NO_ADD_WINDOWS_MANIFEST TRUE)

# Windows-specific GUI subsystem setting
if(WIN32)
  set(GUI_TYPE WIN32)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
    list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/appicon.rc")
    list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/appicon.rc")
  endif()
else()
  set(GUI_TYPE "")
endif()

qt_add_executable(ensemble ${GUI_TYPE} ${SOURCES} ${HEADERS})
set_target_properties(ensemble PROPERTIES QT_NO_ADD_WINDOWS_MANIFEST TRUE)

if(WIN32)
  set_target_properties(Qt6::Core PROPERTIES MAP_IMPORTED_CONFIG_COVERAGE "RELEASE")
  set_target_properties(Qt6::Gui PROPERTIES MAP_IMPORTED_CONFIG_COVERAGE "RELEASE")
  set_target_properties(Qt6::Widgets PROPERTIES MAP_IMPORTED_CONFIG_COVERAGE "RELEASE")
  set_target_properties(Qt6::Network PROPERTIES MAP_IMPORTED_CONFIG_COVERAGE "RELEASE")
  set_target_properties(Qt6::WebEngineWidgets PROPERTIES MAP_IMPORTED_CONFIG_COVERAGE "RELEASE")
  set_target_properties(Qt6::WebSockets PROPERTIES MAP_IMPORTED_CONFIG_COVERAGE "RELEASE")
endif()

target_include_directories(ensemble PRIVATE src)

target_compile_definitions(ensemble PRIVATE
    $<$<CONFIG:Debug>:
        ENSEMBLE_BUILD_TYPE="Debug"
        QT_DEBUG
    >
    $<$<NOT:$<CONFIG:Debug>>:
        ENSEMBLE_BUILD_TYPE="Release"
        QT_NO_DEBUG_OUTPUT
        NDEBUG
    >
    ENSEMBLE_APP_VERSION="${ENSEMBLE_FULL_VERSION}"
    ENSEMBLE_BASE_VERSION="${ENSEMBLE_BASE_VERSION}"
    ENSEMBLE_LOCALVERSION="${ENSEMBLE_LOCALVERSION}"
    ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
)

target_link_libraries(ensemble PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
    Qt6::WebSockets
    ZLIB::ZLIB
)

if(TARGET Qt6::WebEngineQuick)
    target_link_libraries(ensemble PRIVATE Qt6::WebEngineQuick)
    target_compile_definitions(ensemble PRIVATE ENSEMBLE_HAS_WEBENGINE_QUICK=1)
endif()

if(WIN32)
    target_link_libraries(ensemble PRIVATE ws2_32 shlwapi)
endif()

set_target_properties(ensemble PROPERTIES QT_RESOURCE_PREFIX "/")
qt_add_resources(ensemble resources/resources.qrc)

install(TARGETS ensemble RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    install(FILES resources/ensemble.desktop DESTINATION share/applications)
    install(FILES resources/ensemble.metainfo.xml DESTINATION share/metainfo)
    install(FILES src/icons/icon.png DESTINATION share/icons/hicolor/256x256/apps RENAME ensemble.png)
    install(FILES src/icons/icon.png DESTINATION share/pixmaps RENAME ensemble.png)
    install(FILES resources/ensemble-mime.xml DESTINATION share/mime/packages RENAME ensemble.xml)
endif()

# Unit Tests
add_executable(test_ao3_parser test/cpp/test_ao3_parser.cpp src/net/Ao3Parser.cpp)
target_include_directories(test_ao3_parser PRIVATE src)
target_compile_definitions(test_ao3_parser PRIVATE
    $<$<CONFIG:Debug>:
        ENSEMBLE_BUILD_TYPE="Debug"
        QT_DEBUG
    >
    $<$<NOT:$<CONFIG:Debug>>:
        ENSEMBLE_BUILD_TYPE="Release"
        QT_NO_DEBUG_OUTPUT
        NDEBUG
    >
    ENSEMBLE_APP_VERSION="${ENSEMBLE_FULL_VERSION}"
    ENSEMBLE_BASE_VERSION="${ENSEMBLE_BASE_VERSION}"
    ENSEMBLE_LOCALVERSION="${ENSEMBLE_LOCALVERSION}"
    ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
)
target_link_libraries(test_ao3_parser PRIVATE Qt6::Core Qt6::Gui)
if(WIN32)
    target_link_libraries(test_ao3_parser PRIVATE ws2_32)
endif()

add_executable(test_project_archive
    test/cpp/test_project_archive.cpp
    src/model/Chapter.cpp
    src/model/Ao3Project.cpp
    src/model/AgenticData.cpp
    src/model/LoreGraph.cpp
    src/model/LoreManager.cpp
    src/model/ProjectSerializer.cpp
    src/model/ZstdArchive.cpp
    src/utils/AesCipher.cpp
    src/debug/debug.cpp
    src/utils/AppPaths.cpp
    src/utils/miniz.cpp
)
target_include_directories(test_project_archive PRIVATE src)
target_compile_definitions(test_project_archive PRIVATE
    $<$<CONFIG:Debug>:
        ENSEMBLE_BUILD_TYPE="Debug"
        QT_DEBUG
    >
    $<$<NOT:$<CONFIG:Debug>>:
        ENSEMBLE_BUILD_TYPE="Release"
        QT_NO_DEBUG_OUTPUT
        NDEBUG
    >
    ENSEMBLE_APP_VERSION="${ENSEMBLE_FULL_VERSION}"
    ENSEMBLE_BASE_VERSION="${ENSEMBLE_BASE_VERSION}"
    ENSEMBLE_LOCALVERSION="${ENSEMBLE_LOCALVERSION}"
    ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
)
target_link_libraries(test_project_archive PRIVATE Qt6::Core Qt6::Gui Qt6::Network ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(test_project_archive PRIVATE ws2_32)
endif()

add_executable(test_settings_dialog
    test/cpp/test_settings_dialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/HelpGuideDialog.cpp
    src/ui/HelpGuideDialog.h
    src/ui/MainMenuWindow.cpp
    src/ui/MainMenuWindow.h
    src/ui/AppIcon.cpp
    src/themes/themes.cpp
    src/themes/ThemeManager.h
    src/debug/debug.cpp
    src/utils/AppPaths.cpp
    src/utils/SingleInstanceManager.cpp
    src/utils/SingleInstanceManager.h
    src/mcp/McpServer.cpp
    src/mcp/McpServer.h
    src/mcp/McpAgentInstaller.cpp
    src/mcp/McpAgentInstaller.h
)
target_include_directories(test_settings_dialog PRIVATE src)
target_compile_definitions(test_settings_dialog PRIVATE
    $<$<CONFIG:Debug>:
        ENSEMBLE_BUILD_TYPE="Debug"
        QT_DEBUG
    >
    $<$<NOT:$<CONFIG:Debug>>:
        ENSEMBLE_BUILD_TYPE="Release"
        QT_NO_DEBUG_OUTPUT
        NDEBUG
    >
    ENSEMBLE_APP_VERSION="${ENSEMBLE_FULL_VERSION}"
    ENSEMBLE_BASE_VERSION="${ENSEMBLE_BASE_VERSION}"
    ENSEMBLE_LOCALVERSION="${ENSEMBLE_LOCALVERSION}"
    ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
)
target_link_libraries(test_settings_dialog PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets)
if(WIN32)
    target_link_libraries(test_settings_dialog PRIVATE ws2_32)
endif()

add_executable(test_editor_diagnostics
    test/cpp/test_editor_diagnostics.cpp
    src/ui/CodeEditor.cpp
    src/ui/CodeEditor.h
    src/ui/HtmlSourcePane.cpp
    src/ui/HtmlSourcePane.h
    src/ui/CssEditorPane.cpp
    src/ui/CssEditorPane.h
    src/ui/EditorMinimap.cpp
    src/ui/EditorMinimap.h
    src/ui/HtmlSyntaxHighlighter.cpp
    src/ui/HtmlSyntaxHighlighter.h
    src/ui/CssSyntaxHighlighter.cpp
    src/ui/CssSyntaxHighlighter.h
    src/model/Ao3Project.cpp
    src/model/Ao3Project.h
    src/model/AgenticData.cpp
    src/model/AgenticData.h
    src/model/LoreGraph.cpp
    src/model/LoreGraph.h
    src/model/LoreManager.cpp
    src/model/LoreManager.h
    src/utils/AesCipher.cpp
    src/utils/AesCipher.h
    src/model/Chapter.cpp
    src/model/Chapter.h
    src/themes/themes.cpp
    src/themes/ThemeManager.h
    src/debug/debug.cpp
    src/utils/AppPaths.cpp
)
target_include_directories(test_editor_diagnostics PRIVATE src)
target_compile_definitions(test_editor_diagnostics PRIVATE
    $<$<CONFIG:Debug>:
        ENSEMBLE_BUILD_TYPE="Debug"
        QT_DEBUG
    >
    $<$<NOT:$<CONFIG:Debug>>:
        ENSEMBLE_BUILD_TYPE="Release"
        QT_NO_DEBUG_OUTPUT
        NDEBUG
    >
    ENSEMBLE_APP_VERSION="${ENSEMBLE_FULL_VERSION}"
    ENSEMBLE_BASE_VERSION="${ENSEMBLE_BASE_VERSION}"
    ENSEMBLE_LOCALVERSION="${ENSEMBLE_LOCALVERSION}"
    ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
)
target_link_libraries(test_editor_diagnostics PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
if(WIN32)
    target_link_libraries(test_editor_diagnostics PRIVATE ws2_32)
endif()

set(TEST_MCP_SOURCES ${SOURCES})
list(REMOVE_ITEM TEST_MCP_SOURCES "src/main.cpp")

add_executable(test_mcp test/cpp/test_mcp.cpp ${TEST_MCP_SOURCES} ${HEADERS})
target_include_directories(test_mcp PRIVATE src)
target_compile_definitions(test_mcp PRIVATE
    $<$<CONFIG:Debug>:
        ENSEMBLE_BUILD_TYPE="Debug"
        QT_DEBUG
    >
    $<$<NOT:$<CONFIG:Debug>>:
        ENSEMBLE_BUILD_TYPE="Release"
        QT_NO_DEBUG_OUTPUT
        NDEBUG
    >
    ENSEMBLE_APP_VERSION="${ENSEMBLE_FULL_VERSION}"
    ENSEMBLE_BASE_VERSION="${ENSEMBLE_BASE_VERSION}"
    ENSEMBLE_LOCALVERSION="${ENSEMBLE_LOCALVERSION}"
    ENSEMBLE_GIT_DIRTY="${ENSEMBLE_GIT_DIRTY}"
)
target_link_libraries(test_mcp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
    Qt6::WebSockets
    ZLIB::ZLIB
)
if(WIN32)
    target_link_libraries(test_mcp PRIVATE ws2_32 shlwapi)
endif()
set_target_properties(test_mcp PROPERTIES QT_RESOURCE_PREFIX "/")
qt_add_resources(test_mcp resources/resources.qrc)

enable_testing()
add_test(NAME test_ao3_parser COMMAND test_ao3_parser)
add_test(NAME test_project_archive COMMAND test_project_archive)
add_test(NAME test_editor_diagnostics COMMAND test_editor_diagnostics)
add_test(NAME test_settings_dialog COMMAND test_settings_dialog)
add_test(NAME test_mcp COMMAND test_mcp)


