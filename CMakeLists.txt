cmake_minimum_required(VERSION 3.16)
project(FastSearch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------- SQLite3 (compiled as C) ----------
add_library(sqlite3 STATIC third_party/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party)
if(UNIX)
    target_link_libraries(sqlite3 PUBLIC pthread dl m)
endif()
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_THREADSAFE=1
)

# ---------- third-party header-only libs ----------
add_library(third_party INTERFACE)
target_include_directories(third_party INTERFACE third_party)

# ---------- core library ----------
set(CORE_SOURCES
    src/core/database.cpp
    src/core/indexer.cpp
    src/core/searcher.cpp
    src/core/config.cpp
    src/core/watcher.cpp
)

# platform sources
if(WIN32)
    list(APPEND CORE_SOURCES src/platform/platform_win.cpp)
elseif(APPLE)
    list(APPEND CORE_SOURCES src/platform/platform_macos.cpp)
else()
    list(APPEND CORE_SOURCES src/platform/platform_linux.cpp)
endif()

add_library(fastsearch_core STATIC ${CORE_SOURCES})
target_include_directories(fastsearch_core PUBLIC src)
target_link_libraries(fastsearch_core PUBLIC sqlite3 third_party)

if(WIN32)
    target_link_libraries(fastsearch_core PUBLIC ws2_32 shlwapi shell32)
    target_compile_definitions(fastsearch_core PUBLIC
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# ---------- server executable ----------
add_executable(fastsearch-server
    src/server_main.cpp
    src/server/http_server.cpp
    src/server/api_routes.cpp
)
target_link_libraries(fastsearch-server PRIVATE fastsearch_core third_party)

# ---------- CLI executable ----------
add_executable(fastsearch
    src/cli/cli_main.cpp
)
target_link_libraries(fastsearch PRIVATE fastsearch_core third_party)

# ---------- GUI executable (Windows only, WebView2) ----------
if(WIN32)
    add_executable(fastsearch-gui WIN32 src/gui/gui_main.cpp)
    target_include_directories(fastsearch-gui PRIVATE third_party/webview2)
    target_link_directories(fastsearch-gui PRIVATE third_party/webview2)
    target_link_libraries(fastsearch-gui PRIVATE WebView2Loader shlwapi shell32)
    target_compile_definitions(fastsearch-gui PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    add_custom_command(TARGET fastsearch-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/webview2/WebView2Loader.dll"
            "$<TARGET_FILE_DIR:fastsearch-gui>/WebView2Loader.dll"
    )
endif()

# ---------- install ----------
install(TARGETS fastsearch fastsearch-server RUNTIME DESTINATION bin)
