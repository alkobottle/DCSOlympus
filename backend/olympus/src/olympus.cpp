/*
 * olympus.cpp — thin loader DLL loaded by DCS via Lua C API.
 *
 * Intentionally has NO link-time dependencies on our other DLLs (logger, utils,
 * dcstools, luatools). Wine does not propagate the "alternate file search path"
 * (used when LoadLibrary is called with a full path) to transitive dependencies,
 * so any import of e.g. dcstools.dll would fail because dcstools needs luatools.dll
 * which is only in our bin/ directory, not on Wine's standard search path.
 *
 * The fix: this DLL imports only lua.dll + system DLLs (always available).
 * luaopen_olympus calls SetDllDirectoryA(bin/) immediately, so the subsequent
 * LoadLibraryW("core.dll") in onSimulationStart finds all its dependencies.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <string>

#define DllExport __declspec(dllexport)

// ── Runtime-linked core DLL ────────────────────────────────────────────────────
HINSTANCE hGetProcIDDLL = NULL;
typedef int(__stdcall* f_coreInit)(lua_State* L, const char* path);
typedef int(__stdcall* f_coreDeinit)(lua_State* L);
typedef int(__stdcall* f_coreFrame)(lua_State* L);
typedef int(__stdcall* f_coreUnitsData)(lua_State* L);
typedef int(__stdcall* f_coreWeaponsData)(lua_State* L);
typedef int(__stdcall* f_coreMissionData)(lua_State* L);
typedef int(__stdcall* f_coreDrawingsData)(lua_State* L);
typedef int(__stdcall* f_coreSetExecutionResults)(lua_State* L);
f_coreInit coreInit = nullptr;
f_coreDeinit coreDeinit = nullptr;
f_coreFrame coreFrame = nullptr;
f_coreUnitsData coreUnitsData = nullptr;
f_coreWeaponsData coreWeaponsData = nullptr;
f_coreMissionData coreMissionData = nullptr;
f_coreDrawingsData coreDrawingsData = nullptr;
f_coreSetExecutionResults coreExecutionResults = nullptr;

std::string modPath;

// ── Inline helpers (no external DLL deps) ─────────────────────────────────────

static std::wstring to_wstring_local(const std::string& str) {
    if (str.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring ws(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &ws[0], n);
    return ws;
}

static void Log(lua_State* L, const std::string& msg, const char* level_field) {
    lua_getglobal(L, "log");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, level_field);
    int level = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "write");
    lua_remove(L, -2);
    lua_pushstring(L, "Olympus.dll");
    lua_pushnumber(L, level);
    lua_pushstring(L, msg.c_str());
    lua_pcall(L, 3, 0, 0);
}

static void LogInfo(lua_State* L, const std::string& msg) { Log(L, msg, "INFO"); }
static void LogError(lua_State* L, const std::string& msg) { Log(L, msg, "ERROR"); }

static std::string GetLastErrorAsString() {
    DWORD id = ::GetLastError();
    if (id == 0) return {};
    LPSTR buf = nullptr;
    size_t sz = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);
    std::string msg(buf, sz);
    LocalFree(buf);
    return msg;
}

// ── Simulation callbacks ───────────────────────────────────────────────────────

static int onSimulationStart(lua_State* L)
{
    LogInfo(L, "Trying to load core.dll from " + modPath);
    SetDllDirectoryA(modPath.c_str());

    std::string dllLocation = modPath + "\\core.dll";

    hGetProcIDDLL = LoadLibraryW(to_wstring_local(dllLocation).c_str());
    if (!hGetProcIDDLL) {
        LogError(L, "Error loading core DLL: " + GetLastErrorAsString());
        return 0;
    }

    LogInfo(L, "Core DLL loaded successfully");

    coreInit = (f_coreInit)GetProcAddress(hGetProcIDDLL, "coreInit");
    if (!coreInit) { LogError(L, "Error getting coreInit ProcAddress"); goto error; }

    coreDeinit = (f_coreDeinit)GetProcAddress(hGetProcIDDLL, "coreDeinit");
    if (!coreDeinit) { LogError(L, "Error getting coreDeinit ProcAddress"); goto error; }

    coreFrame = (f_coreFrame)GetProcAddress(hGetProcIDDLL, "coreFrame");
    if (!coreFrame) { LogError(L, "Error getting coreFrame ProcAddress"); goto error; }

    coreUnitsData = (f_coreUnitsData)GetProcAddress(hGetProcIDDLL, "coreUnitsData");
    if (!coreUnitsData) { LogError(L, "Error getting coreUnitsData ProcAddress"); goto error; }

    coreWeaponsData = (f_coreWeaponsData)GetProcAddress(hGetProcIDDLL, "coreWeaponsData");
    if (!coreWeaponsData) { LogError(L, "Error getting coreWeaponsData ProcAddress"); goto error; }

    coreMissionData = (f_coreMissionData)GetProcAddress(hGetProcIDDLL, "coreMissionData");
    if (!coreMissionData) { LogError(L, "Error getting coreMissionData ProcAddress"); goto error; }

    coreDrawingsData = (f_coreDrawingsData)GetProcAddress(hGetProcIDDLL, "coreDrawingsData");
    if (!coreDrawingsData) { LogError(L, "Error getting coreDrawingsData ProcAddress"); goto error; }

    coreExecutionResults = (f_coreSetExecutionResults)GetProcAddress(hGetProcIDDLL, "coreSetExecutionResults");
    if (!coreExecutionResults) { LogError(L, "Error getting coreSetExecutionResults ProcAddress"); goto error; }

    coreInit(L, modPath.c_str());
    LogInfo(L, "Module loaded and started successfully.");
    return 0;

error:
    LogError(L, "Error while loading module: " + GetLastErrorAsString());
    return 0;
}

static int onSimulationFrame(lua_State* L)
{
    if (coreFrame) coreFrame(L);
    return 0;
}

static int onSimulationStop(lua_State* L)
{
    if (hGetProcIDDLL) {
        if (coreDeinit) coreDeinit(L);
        if (FreeLibrary(hGetProcIDDLL))
            LogInfo(L, "Core DLL unloaded successfully");
        else
            LogError(L, "Error unloading core DLL: " + GetLastErrorAsString());

        coreInit = nullptr; coreDeinit = nullptr; coreFrame = nullptr;
        coreUnitsData = nullptr; coreWeaponsData = nullptr;
        coreMissionData = nullptr; coreDrawingsData = nullptr;
        coreExecutionResults = nullptr;
    }
    hGetProcIDDLL = NULL;
    return 0;
}

static int setUnitsData(lua_State* L)    { if (coreUnitsData)    coreUnitsData(L);    return 0; }
static int setWeaponsData(lua_State* L)  { if (coreWeaponsData)  coreWeaponsData(L);  return 0; }
static int setMissionData(lua_State* L)  { if (coreMissionData)  coreMissionData(L);  return 0; }
static int setDrawingsData(lua_State* L) { if (coreDrawingsData) coreDrawingsData(L); return 0; }
static int setExecutionResults(lua_State* L) { if (coreExecutionResults) coreExecutionResults(L); return 0; }

static const luaL_Reg Map[] = {
    {"onSimulationStart",    onSimulationStart},
    {"onSimulationFrame",    onSimulationFrame},
    {"onSimulationStop",     onSimulationStop},
    {"setUnitsData",         setUnitsData},
    {"setWeaponsData",       setWeaponsData},
    {"setMissionData",       setMissionData},
    {"setDrawingsData",      setDrawingsData},
    {"setExecutionResults",  setExecutionResults},
    {NULL, NULL}
};

// ── DLL entry point loaded by DCS's Lua runtime ────────────────────────────────

extern "C" DllExport int luaopen_olympus(lua_State* L)
{
    lua_getglobal(L, "require");
    lua_pushstring(L, "lfs");
    lua_pcall(L, 1, 1, 0);
    lua_getfield(L, -1, "writedir");
    lua_pcall(L, 0, 1, 0);

    if (lua_isstring(L, -1)) {
        modPath = std::string(lua_tostring(L, -1)) + "Mods\\Services\\Olympus\\bin\\";
        // Set the DLL search path NOW so that core.dll's dependencies (logger, utils, …)
        // are found in our bin/ directory when LoadLibraryW("core.dll") is called later.
        SetDllDirectoryA(modPath.c_str());
        LogInfo(L, "Instance location retrieved: " + modPath);
    } else {
        // Fallback: log error via raw Lua API (no helper DLLs loaded yet)
        lua_getglobal(L, "log");
        lua_getfield(L, -1, "ERROR");
        int errorLevel = (int)lua_tointeger(L, -1);
        lua_getglobal(L, "log");
        lua_getfield(L, -1, "write");
        lua_pushstring(L, "Olympus.dll");
        lua_pushnumber(L, errorLevel);
        lua_pushstring(L, "Failed to retrieve Olympus instance location");
        lua_pcall(L, 3, 0, 0);
        return 0;
    }

    luaL_register(L, "olympus", Map);
    return 1;
}
