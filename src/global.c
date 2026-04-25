/***********************************************
 *  global.c  —  Global variable definitions
 ***********************************************/

#include "sys.h"

/* Task control blocks */
TCB      g_tasks[MAX_TASKS];
int      g_task_count = 0;

/* Resource control blocks */
RCB      g_resources[MAX_RESOURCES];
int      g_resource_count = 0;

/* System-event bitmask */
TEventMask g_sys_events = 0;

/* Index of currently running task (-1 = no task running) */
int g_running = -1;

/* OS lifecycle flag */
int g_os_running = 0;
