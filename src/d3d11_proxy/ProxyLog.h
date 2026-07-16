#pragma once
//=============================================================================
//  ProxyLog — Minimal file logger for the d3d11 proxy
//
//  No SKSE, no game dependencies. Just writes to a log file.
//  Thread-safe via simple mutex.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <cstdarg>

namespace SB::Proxy
{

inline FILE* g_logFile = nullptr;
inline CRITICAL_SECTION g_logCS = {};
inline bool g_logInitialized = false;

inline void LogInit(const char* path)
{
    if (g_logInitialized) return;
    InitializeCriticalSection(&g_logCS);
    fopen_s(&g_logFile, path, "w");
    g_logInitialized = true;
    if (g_logFile)
        fprintf(g_logFile, "[SkyrimBridge d3d11 Proxy] Log started\n");
}

inline void LogShutdown()
{
    if (!g_logInitialized) return;
    if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
    DeleteCriticalSection(&g_logCS);
    g_logInitialized = false;
}

inline void Log(const char* fmt, ...)
{
    if (!g_logFile) return;
    EnterCriticalSection(&g_logCS);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
    LeaveCriticalSection(&g_logCS);
}

} // namespace SB::Proxy
