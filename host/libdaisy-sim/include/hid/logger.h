#pragma once
#ifndef __DSY_LOGGER_H__
#define __DSY_LOGGER_H__
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
namespace daisy
{
#define LOGGER_NEWLINE "\r\n"
#define LOGGER_BUFFER 128
#define PPCAT_NX(A, B) A##B
#define PPCAT(A, B) PPCAT_NX(A, B)
#define STRINGIZE_NX(A) #A
#define STRINGIZE(A) STRINGIZE_NX(A)
#define FLT_FMT(_n) STRINGIZE(PPCAT(PPCAT(%c%d.%0, _n), d))
#define FLT_VAR(_n, _x) (_x < 0 ? '-' : ' '), (int)(abs(_x)), (int)(((abs(_x)) - (int)(abs(_x))) * pow(10, (_n)))
#define FLT_FMT3 FLT_FMT(3)
#define FLT_VAR3(_x) FLT_VAR(3, _x)

enum LoggerDestination { LOGGER_NONE, LOGGER_INTERNAL, LOGGER_EXTERNAL, LOGGER_SEMIHOST };

/** Implemented by the simulator core: appends to the log the front-end can read. */
void chompi_sim_log_vprint(const char* format, va_list va, bool newline);

/** Debug log of the simulated device: lines go to Sim::TakeLog() and stderr. */
template <LoggerDestination dest = LOGGER_INTERNAL>
class Logger
{
  public:
    Logger() {}
    static void Print(const char* format, ...)
    {
        va_list va;
        va_start(va, format);
        chompi_sim_log_vprint(format, va, false);
        va_end(va);
    }
    static void PrintLine(const char* format, ...)
    {
        va_list va;
        va_start(va, format);
        chompi_sim_log_vprint(format, va, true);
        va_end(va);
    }
    static void StartLog(bool wait_for_pc = false) { (void)wait_for_pc; }
    static void PrintV(const char* format, va_list va) { chompi_sim_log_vprint(format, va, false); }
    static void PrintLineV(const char* format, va_list va) { chompi_sim_log_vprint(format, va, true); }
};
template <>
class Logger<LOGGER_NONE>
{
  public:
    Logger() {}
    static void Print(const char*, ...) {}
    static void PrintLine(const char*, ...) {}
    static void StartLog(bool = false) {}
    static void PrintV(const char*, va_list) {}
    static void PrintLineV(const char*, va_list) {}
};
} // namespace daisy
#endif
