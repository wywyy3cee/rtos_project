/***********************************************
 *  sys.h  —  Internal OS data structures
 *  (not exposed to user applications)
 ***********************************************/

#ifndef SYS_H
#define SYS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtos_api.h"

/* ─── Task states ─────────────────────────────────────────────────────────*/
typedef enum {
    TASK_SUSPENDED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING   /* waiting for a system event */
} TaskState;

/* ─── TCB – Task Control Block ────────────────────────────────────────────*/
typedef struct {
    void       (*func)(void);           /* task function pointer            */
    int         base_priority;          /* static RMA priority              */
    int         cur_priority;           /* current (possibly inherited) pri */
    TaskState   state;
    const char *name;                   /* debug name                       */
    TEventMask  wait_mask;              /* events the task is waiting for   */
    int         woken_by_event;         /* 1 if SetSysEvent woke this task  */
    /* PIP: stack of resources held, kept as a simple array */
    TResource   held_resources[MAX_RESOURCES];
    int         held_count;
} TCB;

/* ─── RCB – Resource Control Block ───────────────────────────────────────*/
typedef struct {
    const char *name;
    int         owner;          /* TTask of current owner, -1 if free      */
    int         saved_priority; /* owner's priority before acquisition      */
} RCB;

/* ─── Global state (defined in global.c) ─────────────────────────────────*/
extern TCB      g_tasks[MAX_TASKS];
extern int      g_task_count;

extern RCB      g_resources[MAX_RESOURCES];
extern int      g_resource_count;

extern TEventMask g_sys_events;         /* system-event bitmask             */

extern int      g_running;              /* index of currently running task, -1 if none */
extern int      g_os_running;           /* 1 after StartOS, 0 before/after  */

/* ─── Internal scheduler / dispatcher ────────────────────────────────────*/
/* Select highest-priority READY task (by base_priority for nonpreemptive RMA).
   Returns task index or -1. */
int  Scheduler_SelectNext(void);

/* Run the task queue until no ready tasks remain or ShutdownOS called. */
void Dispatcher_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_H */
