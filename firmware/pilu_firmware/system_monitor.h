#ifndef PILU_SYSTEM_MONITOR_H
#define PILU_SYSTEM_MONITOR_H

#include <Arduino.h>

/**
 * @file system_monitor.h
 * @brief System stability, memory heap tracking, and operational diagnostics.
 */

void system_monitor_init();
void system_monitor_process();
void system_monitor_print_status();

uint32_t system_monitor_get_free_heap();
uint32_t system_monitor_get_min_free_heap();
uint32_t system_monitor_get_uptime_seconds();

#endif // PILU_SYSTEM_MONITOR_H
