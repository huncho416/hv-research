/*
 * log.h - Minimal leveled logging for the research driver.
 *
 * Output goes to the kernel debugger (DbgPrintEx). Attach WinDbg/kd to the
 * research VM to capture evidence for the certificate. Verbosity is compiled
 * in; raise HVR_LOG_LEVEL for stage bring-up, lower it for perf-sensitive runs.
 */
#pragma once
#include <ntddk.h>

#define HVR_LOG_LEVEL_ERROR 0
#define HVR_LOG_LEVEL_WARN  1
#define HVR_LOG_LEVEL_INFO  2
#define HVR_LOG_LEVEL_TRACE 3

#ifndef HVR_LOG_LEVEL
#define HVR_LOG_LEVEL HVR_LOG_LEVEL_INFO
#endif

#define HVR_TAG 'rsvH'  /* 'Hvsr' pool tag */

#define HVR_LOG(level, prefix, fmt, ...)                                        \
    do {                                                                       \
        if ((level) <= HVR_LOG_LEVEL) {                                         \
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,                 \
                       "[hvr] " prefix " " fmt "\n", ##__VA_ARGS__);            \
        }                                                                      \
    } while (0)

#define HVR_ERR(fmt, ...)   HVR_LOG(HVR_LOG_LEVEL_ERROR, "ERR ", fmt, ##__VA_ARGS__)
#define HVR_WARN(fmt, ...)  HVR_LOG(HVR_LOG_LEVEL_WARN,  "WARN", fmt, ##__VA_ARGS__)
#define HVR_INFO(fmt, ...)  HVR_LOG(HVR_LOG_LEVEL_INFO,  "INFO", fmt, ##__VA_ARGS__)
#define HVR_TRACE(fmt, ...) HVR_LOG(HVR_LOG_LEVEL_TRACE, "TRC ", fmt, ##__VA_ARGS__)
