/*
 * work_queue.cpp - Work-stealing pool global instance
 *
 * Extracted from keyhunt.cpp. Defines the global g_work_pool instance.
 * WorkPool struct is defined in core/workpool.h.
 */

#include "work_queue.h"

/* Global work pool instance */
WorkPool g_work_pool;
