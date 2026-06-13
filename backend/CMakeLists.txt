cmake_minimum_required(VERSION 3.20)
project(OlympusBackend CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths ──────────────────────────────────────────────────────────────────────
set(BACKEND      ${CMAKE_SOURCE_DIR})
set(THIRD_PARTY  ${CMAKE_SOURCE_DIR}/../third-party)
set(MINGW_DEPS   ${CMAKE_SOURCE_DIR}/../mingw-deps)

set(CPPREST_SRC  ${THIRD_PARTY}/cpprestsdk/Release/src)
set(CPPREST_INC  ${THIRD_PARTY}/cpprestsdk/Release/include)
set(LUA_INC      ${THIRD_PARTY}/lua/include)
set(LUA_LIB      ${THIRD_PARTY}/lua/liblua.a)
set(GEOG_INC     ${MINGW_DEPS}/include)
set(GEOG_LIB     ${MINGW_DEPS}/lib/libGeographicLib.a)
set(HTTPLIB_INC  ${THIRD_PARTY}/httplib)
set(BASE64_INC   ${THIRD_PARTY}/base64/include)

# ── Global compile settings ────────────────────────────────────────────────────
# _WIN32_WINNT 0x0601 = Windows 7, matching the original vcxproj setting.
# _UTF16_STRINGS: tells cpprestsdk to use wstring as string_t and enables the
#   std::string overload for json::value::parse() that server.cpp uses.
# _NO_ASYNCRTIMP: strips all __declspec(dllimport/dllexport) from cpprestsdk
#   headers so the JSON subset can be compiled as a static library into each
#   DLL. This avoids cross-DLL import issues on MinGW/Wine.
# PPLXTASKS_H / _PPLX_H: block the pplx task headers. asyncrt_utils.h pulls
#   them in but the JSON code never actually uses pplx::task types.
add_compile_definitions(
    WIN32 _WIN32
    WINVER=0x0601 _WIN32_WINNT=0x0601
    _UTF16_STRINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _NO_ASYNCRTIMP
    PPLXTASKS_H
    _PPLX_H
)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unknown-pragmas")

# Link MinGW runtimes statically so the DLLs don't need libgcc_s_seh-1.dll /
# libstdc++-6.dll deployed alongside them in the Wine environment.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# DCS loads olympus.dll / core.dll by name — strip MinGW's "lib" prefix
set(CMAKE_SHARED_LIBRARY_PREFIX "")

# Common include directories shared by every target
include_directories(
    ${BACKEND}/shared/include
    ${LUA_INC}
    ${CPPREST_INC}
)

# ── cpprest_json (OBJECT library — JSON-only subset of cpprestsdk) ─────────────
# The 4 source files include "stdafx.h". We replace it with a minimal version
# (cmake/cpprest_pch/) that blocks the HTTP/PPL chain.
# Compiled as OBJECT so the .o files are linked directly into each consuming DLL
# (no separate cpprest_json.dll needed at runtime — simpler Wine deployment).
add_library(cpprest_json OBJECT
    ${CPPREST_SRC}/json/json.cpp
    ${CPPREST_SRC}/json/json_parsing.cpp
    ${CPPREST_SRC}/json/json_serialization.cpp
    ${CPPREST_SRC}/utilities/asyncrt_utils.cpp
)
target_include_directories(cpprest_json BEFORE PRIVATE
    ${BACKEND}/cmake/cpprest_pch
    ${CPPREST_SRC}/pch
)

# ── utils ──────────────────────────────────────────────────────────────────────
add_library(utils SHARED utils/src/utils.cpp $<TARGET_OBJECTS:cpprest_json>)
target_include_directories(utils PRIVATE utils/include)
target_link_libraries(utils PRIVATE ${LUA_LIB})

# ── logger ─────────────────────────────────────────────────────────────────────
add_library(logger SHARED
    logger/src/interface.cpp
    logger/src/logger.cpp
    $<TARGET_OBJECTS:cpprest_json>
)
target_include_directories(logger PRIVATE logger/include utils/include)
target_link_libraries(logger PRIVATE utils)

# ── luatools ───────────────────────────────────────────────────────────────────
add_library(luatools SHARED luatools/src/luatools.cpp $<TARGET_OBJECTS:cpprest_json>)
target_include_directories(luatools PRIVATE luatools/include utils/include logger/include)
target_link_libraries(luatools PRIVATE logger utils ${LUA_LIB})

# ── dcstools ───────────────────────────────────────────────────────────────────
add_library(dcstools SHARED dcstools/src/dcstools.cpp $<TARGET_OBJECTS:cpprest_json>)
target_include_directories(dcstools PRIVATE
    dcstools/include luatools/include utils/include logger/include
)
target_link_libraries(dcstools PRIVATE luatools utils ${LUA_LIB})

# ── core ───────────────────────────────────────────────────────────────────────
file(GLOB CORE_SOURCES core/src/*.cpp)
add_library(core SHARED ${CORE_SOURCES} $<TARGET_OBJECTS:cpprest_json>)
target_include_directories(core PRIVATE
    core/include
    dcstools/include luatools/include logger/include utils/include
    ${BASE64_INC}
    ${HTTPLIB_INC}
    ${GEOG_INC}
)
target_link_libraries(core PRIVATE
    dcstools luatools logger utils
    ${LUA_LIB}
    ${GEOG_LIB}
    ws2_32          # WinSock2 — required by cpp-httplib
)

# ── olympus ────────────────────────────────────────────────────────────────────
# Intentionally no link-time dependency on our custom DLLs: Wine does not
# propagate the alternate file search path to transitive dependencies, so
# dcstools/luatools/etc. would not be found at load time. olympus.cpp is
# self-contained; it only needs lua.dll + system DLLs (always available in DCS).
# SetDllDirectoryA is called inside luaopen_olympus so that the subsequent
# LoadLibraryW("core.dll") in onSimulationStart finds all transitive deps.
add_library(olympus SHARED olympus/src/olympus.cpp)
target_include_directories(olympus PRIVATE ${LUA_INC})
target_link_libraries(olympus PRIVATE ${LUA_LIB})
