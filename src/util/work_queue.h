/*
 * work_queue.h - Work-stealing pool global instance
 *
 * Provides the global g_work_pool instance (defined in work_queue.cpp).
 * The WorkPool struct is defined in core/workpool.h.
 *
 * Previously g_work_pool was defined in keyhunt.cpp. Extracting it here
 * allows search modules to include this header instead of using extern.
 */

#ifndef KEYHUNT_WORK_QUEUE_H
#define KEYHUNT_WORK_QUEUE_H

#ifdef __cplusplus

#include "../core/workpool.h"

/* Global work pool instance (defined in util/work_queue.cpp) */
extern WorkPool g_work_pool;

#endif /* __cplusplus */

#endif /* KEYHUNT_WORK_QUEUE_H */
