cmake_minimum_required(VERSION 3.16)

# Version source of truth, in priority order:
#   1. APP_VERSION env var (CI passes the release tag here).
#   2. `git describe --tags --dirty` — needs at least one tag in the repo.
#   3. Fallback baseline "0.0.1" — used while the repo has no tags yet, so the
#      footer still shows a real-looking version instead of a bare commit hash.
if(DEFINED ENV{APP_VERSION})
    set(APP_VERSION "$ENV{APP_VERSION}")
else()
    execute_process(
        COMMAND git describe --tags --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_DESCRIBE)
        # Strip a leading "v" so "v1.2.3" renders as "1.2.3" (the footer prepends it).
        string(REGEX REPLACE "^v" "" APP_VERSION "${GIT_DESCRIBE}")
    else()
        set(APP_VERSION "dev")
    endif()
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_SHORT_HASH)
    set(GIT_SHORT_HASH "unknown")
endif()
message(STATUS "hashhush version: ${APP_VERSION} (${GIT_SHORT_HASH})")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY
)
configure_file(
    ${CMAKE_SOURCE_DIR}/web/src/version.js.in
    ${CMAKE_SOURCE_DIR}/web/src/version.js
    @ONLY
)

find_package(SQLite3 REQUIRED)

add_executable(hashhush
    main.cpp
    config.cpp
    db.cpp
    room.cpp
    util.cpp
    webapi.cpp
    config/inireader.cpp
    config/ini_backend/ini.c
    sha256/sha256.cpp
)

target_include_directories(hashhush PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(hashhush PRIVATE CROW_ENFORCE_WS_SPEC)

target_link_libraries(hashhush PRIVATE
    SQLite::SQLite3
    pthread
)

include(GNUInstallDirs)
install(TARGETS hashhush RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
