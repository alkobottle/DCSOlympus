#pragma once
// Minimal precompiled header for cpprestsdk JSON-only subset.
// Replaces the full stdafx.h which pulls in HTTP.sys, WinHTTP, PPL tasks, Boost, etc.
// WIN32_LEAN_AND_MEAN, NOMINMAX, _NO_ASYNCRTIMP, PPLXTASKS_H come from CMake defines.

#include <windows.h>

// Error codes used in asyncrt_utils.cpp's HTTP error mapping. We define them
// directly to avoid pulling in winhttp.h/wininet.h which conflict with each other.
#ifndef WINHTTP_ERROR_BASE
#define WINHTTP_ERROR_BASE              12000
#define ERROR_WINHTTP_TIMEOUT           (WINHTTP_ERROR_BASE + 2)
#define ERROR_WINHTTP_CANNOT_CONNECT    (WINHTTP_ERROR_BASE + 29)
#define ERROR_WINHTTP_CONNECTION_ERROR  (WINHTTP_ERROR_BASE + 30)
#endif
#ifndef INET_E_RESOURCE_NOT_FOUND
#define INET_E_RESOURCE_NOT_FOUND   ((HRESULT)0x800C0005L)
#define INET_E_CANNOT_CONNECT       ((HRESULT)0x800C0004L)
#define INET_E_CONNECTION_TIMEOUT   ((HRESULT)0x800C000BL)
#define INET_E_DOWNLOAD_FAILURE     ((HRESULT)0x800C0008L)
#endif

// MinGW's winnt.h defines C_ASSERT(e) as "extern void __C_ASSERT__(...)" in C++ mode,
// which fails inside template class bodies (SafeInt3.hpp). Replace with static_assert.
#ifdef C_ASSERT
#undef C_ASSERT
#endif
#define C_ASSERT(e) static_assert(e, #e)

// Standard library
#include <algorithm>
#include <array>
#include <assert.h>
#include <cstring>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <vector>

// cpprestsdk type machinery
#include "cpprest/details/basic_types.h"
#include "cpprest/details/cpprest_compat.h"
#include "cpprest/version.h"

// JSON and string utilities — the only parts we compile
#include "cpprest/json.h"
#include "cpprest/asyncrt_utils.h"

#define _CASA_BUILD_FROM_SRC
