/***********************************************
 *  task.c  —  Task management
 *
 *  Scheduler  : flat, nonpreemptive, RMA
 *    - "flat"         : single ready-queue (no groups/nesting)
 *    - "nonpreemptive": once a task starts it runs until TerminateTask or
 *                       WaitSysEvent (voluntary yield)
 *    - "RMA"          : tasks are ordered by static base_priority (higher = first)
 *                       equal-priority tasks run in FIFO activation order
 ***********************************************/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "sys.h"

/* ─── Internal helpers ────────────────────────────────────────────────────*/

/* Find the READY task with the highest base_priority.
   In case of tie, choose the one activated earliest (lower index wins,
   since we fill slots in order). */
int Scheduler_SelectNext(void)
{
    int best = -1;
    int best_prio = -1;

    int i;
    for (i = 0; i < g_task_count; i++) {
        if (g_tasks[i].state == TASK_READY) {
            if (g_tasks[i].base_priority > best_prio) {
                best_prio = g_tasks[i].base_priority;
                best = i;
            }
        }
    }
    return best;
}

/* ─── Dispatcher ──────────────────────────────────────────────────────────
 * Runs tasks one at a time (nonpreemptive) until:
 *  - no READY task exists, or
 *  - g_os_running becomes 0 (ShutdownOS was called).
 *
 * Each task runs to completion (TerminateTask) or until it calls
 * WaitSysEvent (which sets its state to WAITING and returns here).
 */
void Dispatcher_Run(void)
{
    while (g_os_running) {
        int next = Scheduler_SelectNext();
        if (next < 0) {
            /* No ready tasks — check if any tasks are waiting;
               if not, we're done. */
            int i, any_waiting = 0;
            for (i = 0; i < g_task_count; i++) {
                if (g_tasks[i].state == TASK_WAITING) { any_waiting = 1; break; }
            }
            if (!any_waiting) break; /* all done */
            /* Waiting tasks can only be unblocked by SetSysEvent which is
               called from another task or (conceptually) an ISR.
               In a pure software simulation with no external events we
               must break to avoid an infinite loop. */
            break;
        }

        g_tasks[next].state = TASK_RUNNING;
        g_running = next;

#ifdef RTOS_DEBUG
        printf("[RTOS] Dispatch -> %s (prio=%d)\n",
               g_tasks[next].name, g_tasks[next].base_priority);
#endif

        g_tasks[next].func(); /* run the task body */

        /* After the task returns control comes back here.
           (TerminateTask sets state=SUSPENDED before returning from func.) */
        g_running = -1;
    }
}

/* ─── Public API ──────────────────────────────────────────────────────────*/

TTask RegisterTask(void (*func)(void), int priority, const char *name)
{
    assert(func != NULL);
    assert(g_task_count < MAX_TASKS);

    TTask id = g_task_count++;
    TCB *t = &g_tasks[id];

    t->func          = func;
    t->base_priority = priority;
    t->cur_priority  = priority;
    t->state         = TASK_SUSPENDED;
    t->name          = name;
    t->wait_mask     = 0;
    t->woken_by_event = 0;
    t->held_count    = 0;

    return id;
}

void ActivateTask(TTask taskId)
{
    assert(taskId >= 0 && taskId < g_task_count);
    TCB *t = &g_tasks[taskId];

    /* Spec: repeated activation of an already-active task is forbidden */
    assert(t->state == TASK_SUSPENDED &&
           "ActivateTask: task is not in SUSPENDED state");

#ifdef RTOS_DEBUG
    printf("[RTOS] ActivateTask(%s)\n", t->name);
#endif

    t->state          = TASK_READY;
    t->cur_priority   = t->base_priority;
    t->held_count     = 0;
    t->wait_mask      = 0;
    t->woken_by_event = 0;

    /* Nonpreemptive: we do NOT dispatch here even if new task has higher
       priority.  The running task continues; scheduler picks next task when
       the current one terminates or waits. */
}

void TerminateTask(void)
{
    /* After ShutdownOS the dispatcher may have cleared g_running;
       in that case TerminateTask is a no-op so task functions can
       still call it without crashing. */
    if (g_running < 0) return;
    assert(g_running >= 0 && "TerminateTask called outside a task context");

    TCB *t = &g_tasks[g_running];

    /* Spec: must not be inside a critical section */
    assert(t->held_count == 0 &&
           "TerminateTask called while holding a resource");

#ifdef RTOS_DEBUG
    printf("[RTOS] TerminateTask(%s)\n", t->name);
#endif

    t->state      = TASK_SUSPENDED;
    t->wait_mask  = 0;
    t->held_count = 0;
    /* g_running reset by Dispatcher_Run after func() returns */
}
