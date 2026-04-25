/***********************************************
 *  os.c  —  OS lifecycle + system-event management
 *
 *  System events:
 *    A single global bitmask g_sys_events.
 *    Any task can set/clear/wait on any combination of bits.
 *
 *    SetSysEvent semantics (per spec §4.4.2):
 *      All tasks waiting on any matched bit are moved to READY.
 *      The bits that caused each wakeup are cleared (unless another
 *      still-WAITING task needs the same bits).
 *
 *    WaitSysEvent semantics:
 *      - If already woken by SetSysEvent (woken_by_event flag): return.
 *      - If any requested bit already set in g_sys_events: clear & return.
 *      - Otherwise: suspend (WAITING) until SetSysEvent fires.
 ***********************************************/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "sys.h"

/* ─── OS lifecycle ────────────────────────────────────────────────────────*/

void StartOS(TTask firstTask)
{
    assert(!g_os_running && "StartOS called while OS is already running");
    assert(firstTask >= 0 && firstTask < g_task_count);

#ifdef RTOS_DEBUG
    printf("[RTOS] StartOS — first task: %s\n", g_tasks[firstTask].name);
#endif

    g_os_running = 1;
    g_sys_events = 0;

    ActivateTask(firstTask);
    Dispatcher_Run();

    g_os_running = 0;

#ifdef RTOS_DEBUG
    printf("[RTOS] StartOS — returning to caller\n");
#endif
}

void ShutdownOS(void)
{
#ifdef RTOS_DEBUG
    printf("[RTOS] ShutdownOS\n");
#endif
    g_os_running = 0;
}

/* ─── System-event management ─────────────────────────────────────────────*/

void SetSysEvent(TEventMask mask)
{
    int i, j;

#ifdef RTOS_DEBUG
    printf("[RTOS] SetSysEvent(0x%X)\n", (unsigned)mask);
#endif

    g_sys_events |= mask;

    /* Pass 1: move all matching WAITING tasks to READY, mark them woken */
    for (i = 0; i < g_task_count; i++) {
        if (g_tasks[i].state == TASK_WAITING &&
            (g_tasks[i].wait_mask & mask) != 0)
        {
#ifdef RTOS_DEBUG
            printf("[RTOS] SetSysEvent: waking %s\n", g_tasks[i].name);
#endif
            g_tasks[i].state          = TASK_READY;
            g_tasks[i].woken_by_event = 1;
            /* Keep wait_mask for pass 2 */
        }
    }

    /* Pass 2: clear from g_sys_events the bits consumed by woken tasks,
     * but only if no still-WAITING task also needs those bits. */
    for (i = 0; i < g_task_count; i++) {
        if (g_tasks[i].state == TASK_READY && g_tasks[i].woken_by_event &&
            g_tasks[i].wait_mask != 0)
        {
            TEventMask bits_to_clear = g_tasks[i].wait_mask & mask;

            /* Preserve bits still needed by other WAITING tasks */
            for (j = 0; j < g_task_count; j++) {
                if (g_tasks[j].state == TASK_WAITING)
                    bits_to_clear &= ~g_tasks[j].wait_mask;
            }
            g_sys_events     &= ~bits_to_clear;
            g_tasks[i].wait_mask = 0; /* consumed */
        }
    }
}

void ClearSysEvent(TEventMask mask)
{
#ifdef RTOS_DEBUG
    printf("[RTOS] ClearSysEvent(0x%X)\n", (unsigned)mask);
#endif
    g_sys_events &= ~mask;
}

void GetSysEvent(TEventMask *mask)
{
    assert(mask != NULL);
    *mask = g_sys_events;
}

void WaitSysEvent(TEventMask mask)
{
    assert(g_running >= 0 && "WaitSysEvent called outside a task context");
    assert(mask != 0 && "WaitSysEvent: empty mask");

    TCB *me = &g_tasks[g_running];

    /* Case 1: we were woken by SetSysEvent (flag set, wait_mask cleared) */
    if (me->woken_by_event) {
        me->woken_by_event = 0;
#ifdef RTOS_DEBUG
        printf("[RTOS] WaitSysEvent(0x%X) — %s already woken by SetSysEvent\n",
               (unsigned)mask, me->name);
#endif
        return;
    }

    /* Case 2: event already set in global mask (no prior SetSysEvent wakeup) */
    if ((g_sys_events & mask) != 0) {
        TEventMask matched = g_sys_events & mask;
        g_sys_events &= ~matched;
#ifdef RTOS_DEBUG
        printf("[RTOS] WaitSysEvent(0x%X) — already set, returning immediately\n",
               (unsigned)mask);
#endif
        return;
    }

    /* Case 3: event not set — suspend */
#ifdef RTOS_DEBUG
    printf("[RTOS] WaitSysEvent(0x%X) — %s going WAITING\n",
           (unsigned)mask, me->name);
#endif

    me->state          = TASK_WAITING;
    me->wait_mask      = mask;
    me->woken_by_event = 0;
    g_running = -1;

    /* Yield to dispatcher; resume here after SetSysEvent + redispatch */
    Dispatcher_Run();

    /* woken_by_event was cleared in the fast-path above (Case 1) when
     * the dispatcher re-runs this function body. No extra cleanup needed. */
}
