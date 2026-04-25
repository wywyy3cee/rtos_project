/***********************************************
 *  rtos_api.h  —  Public RTOS API
 *
 *  Individual task #4:
 *    Scheduler  : flat, nonpreemptive, RMA (static priorities)
 *    Resources  : PIP (Priority Inheritance Protocol)
 *    Events     : system events
 *    Interrupts : none
 *
 *  Limits: tasks=32, resources=16, events=16
 ***********************************************/

#ifndef RTOS_API_H
#define RTOS_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ─── Types ─────────────────────────────────────────────────────────────── */

typedef int TTask;        /* task identifier (index into TCB table)         */
typedef int TResource;    /* resource identifier (index into RCB table)     */
typedef uint32_t TEventMask; /* bitmask of system events (up to 32 bits)   */

/* ─── Limits ─────────────────────────────────────────────────────────────── */

#define MAX_TASKS     32
#define MAX_RESOURCES 16
#define MAX_EVENTS    16

/* ─── Task declaration / definition macros ────────────────────────────────
 *
 *  Usage:
 *    DeclareTask(myTask);          // forward declaration (in header / before use)
 *    TASK(myTask, 5) { ... TerminateTask(); }  // definition with priority
 *
 *  The priority is stored at compile time via an enum constant
 *  named  <TaskID>_priority.
 */

#define DeclareTask(TaskID) \
    void TaskID##_func(void); \
    extern TTask TaskID

/* TASK macro:  TASK(name, priority) { body }
 * Expands to the function body AND registers the static priority. */
#define TASK(TaskID, priority) \
    enum { TaskID##_priority = (priority) }; \
    void TaskID##_func(void)

/* ─── Resource declaration macro ──────────────────────────────────────────
 *  DeclareResource(ResID)  — just a forward extern; actual RCB allocated
 *  at runtime via InitRes().
 */
#define DeclareResource(ResID) \
    extern TResource ResID

/* ─── System-event declaration macro ──────────────────────────────────────
 *  DeclareSysEvent(EventID, bit)  where bit is 0..MAX_EVENTS-1.
 *  Creates a compile-time constant  EventID  equal to (1u << bit).
 */
#define DeclareSysEvent(EventID, bit) \
    enum { EventID = (1u << (bit)) }

/* ─── Task management ─────────────────────────────────────────────────────*/

/**
 * ActivateTask  — move task from suspended → ready.
 *   taskId   : identifier returned by RegisterTask() / stored in DeclareTask var
 *   Repeated activation of an already-active task is undefined behaviour
 *   (we assert in debug builds).
 */
void ActivateTask(TTask taskId);

/**
 * TerminateTask  — terminate the currently running task (running → suspended).
 * Must be the last call made by every task.
 * Must NOT be called from inside a critical section.
 */
void TerminateTask(void);

/* ─── Resource management (PIP) ───────────────────────────────────────────*/

/**
 * InitRes  — register a resource.  Call before use.
 *   resId : value returned; use as DeclareResource variable
 */
void InitRes(TResource resId);

/**
 * PIP_GetRes  — acquire resource (enter critical section, PIP).
 */
void PIP_GetRes(TResource resId);

/**
 * PIP_ReleaseRes  — release resource (leave critical section, PIP).
 */
void PIP_ReleaseRes(TResource resId);

/* ─── System-event management ─────────────────────────────────────────────*/

/**
 * SetSysEvent  — set one or more system-event bits; wake any waiting tasks.
 */
void SetSysEvent(TEventMask mask);

/**
 * ClearSysEvent  — clear one or more system-event bits.
 */
void ClearSysEvent(TEventMask mask);

/**
 * GetSysEvent  — read current system-event mask (does not clear it).
 */
void GetSysEvent(TEventMask *mask);

/**
 * WaitSysEvent  — suspend calling task until at least one bit in mask fires.
 * On return the matching bits are cleared from the system mask.
 */
void WaitSysEvent(TEventMask mask);

/* ─── OS lifecycle ────────────────────────────────────────────────────────*/

/**
 * RegisterTask  — register a task with the OS before StartOS.
 *   func     : pointer to the task function (TaskID_func)
 *   priority : static RMA priority (higher value = higher priority)
 *   name     : human-readable name (for debug output)
 *   Returns  : TTask identifier to store in the DeclareTask variable,
 *              or -1 on error.
 */
TTask RegisterTask(void (*func)(void), int priority, const char *name);

/**
 * RegisterRes  — register a resource, returns TResource id.
 */
TResource RegisterRes(const char *name);

/**
 * StartOS  — initialise and run the OS.
 *   firstTask : task to activate immediately.
 * Returns only after ShutdownOS() is called.
 */
void StartOS(TTask firstTask);

/**
 * ShutdownOS  — stop the OS; returns control to the caller of StartOS.
 */
void ShutdownOS(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_API_H */
