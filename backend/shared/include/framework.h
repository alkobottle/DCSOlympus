#pragma once

#define DllExport   __declspec( dllexport )

// Windows Header Files
#include <windows.h>

// MinGW's winnt.h defines C_ASSERT(e) as "extern void __C_ASSERT__(...)" in C++ mode,
// which fails inside template/class bodies (SafeInt3.hpp). Replace with static_assert.
#ifdef C_ASSERT
#undef C_ASSERT
#endif
#define C_ASSERT(e) static_assert(e, #e)

#include <iostream>
#include <string>
#include <time.h>
#include <chrono>
#include <string>
#include <map>
#include <list>
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <filesystem>
#include <codecvt>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <cpprest/json.h>
#include <set>

using namespace std;
using namespace web;

extern "C" {
    #include "lua.h"		  
    #include "lualib.h"
    #include "luaconf.h"
    #include "lauxlib.h"
}