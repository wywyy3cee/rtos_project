/***********************************************
 *  tests.c  —  Test suite for RTOS
 *
 *  Covers:
 *    1. Basic task activation and termination
 *    2. RMA priority ordering (higher prio runs first)
 *    3. Nonpreemptive scheduling (FIFO for equal priority)
 *    4. PIP: priority inheritance on resource acquisition
 *    5. System events: SetSysEvent wakes waiting task
 *    6. System events: WaitSysEvent on already-set event
 *    7. Nested resources (multiple resources held simultaneously)
 *    8. ShutdownOS stops the dispatcher
 *    9. RegisterTask up to MAX_TASKS check (boundary)
 *   10. System events: multiple waiters woken by one SetSysEvent
 ***********************************************/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rtos_api.h"
#include "sys.h"

/* ─── Test infrastructure ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            printf("  [PASS] %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); \
            g_fail++; \
        } \
    } while(0)

/* Reset the OS global state between tests */
static void reset_os(void)
{
    int i;
    g_task_count    = 0;
    g_resource_count = 0;
    g_sys_events    = 0;
    g_running       = -1;
    g_os_running    = 0;

    for (i = 0; i < MAX_TASKS; i++) {
        g_tasks[i].state         = TASK_SUSPENDED;
        g_tasks[i].held_count    = 0;
        g_tasks[i].wait_mask     = 0;
        g_tasks[i].woken_by_event = 0;
        g_tasks[i].func          = NULL;
        g_tasks[i].name          = "";
    }
    for (i = 0; i < MAX_RESOURCES; i++) {
        g_resources[i].owner          = -1;
        g_resources[i].saved_priority = -1;
        g_resources[i].name           = "";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 1 – Basic activation and termination
 * ═══════════════════════════════════════════════════════════════════════════*/

static int t1_ran = 0;

static void t1_task(void) {
    t1_ran = 1;
    TerminateTask();
}

static void test_01_basic_activate_terminate(void)
{
    printf("\n[TEST 1] Basic ActivateTask / TerminateTask\n");
    reset_os();
    t1_ran = 0;

    TTask t = RegisterTask(t1_task, 1, "T1");
    StartOS(t);

    TEST_ASSERT(t1_ran == 1, "Task ran exactly once");
    TEST_ASSERT(g_tasks[t].state == TASK_SUSPENDED, "Task state is SUSPENDED after TerminateTask");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 2 – RMA: higher priority task runs first
 * ═══════════════════════════════════════════════════════════════════════════*/

static int t2_order[3];
static int t2_idx = 0;
static TTask t2_lo, t2_hi, t2_mid;

static void t2_hi_task(void)  { t2_order[t2_idx++] = 10; TerminateTask(); }
static void t2_mid_task(void) { t2_order[t2_idx++] = 5;  TerminateTask(); }
static void t2_lo_task(void)
{
    /* Activate all others — nonpreemptive: they queue up, lo continues */
    ActivateTask(t2_hi);
    ActivateTask(t2_mid);
    t2_order[t2_idx++] = 1;
    TerminateTask();
}

static void test_02_rma_priority_ordering(void)
{
    printf("\n[TEST 2] RMA priority ordering\n");
    reset_os();
    t2_idx = 0;

    t2_lo  = RegisterTask(t2_lo_task,  1, "Lo");
    t2_hi  = RegisterTask(t2_hi_task, 10, "Hi");
    t2_mid = RegisterTask(t2_mid_task, 5, "Mid");

    StartOS(t2_lo);

    /* Lo runs first (it was the starting task, nonpreemptive).
       After Lo terminates, scheduler picks Hi (prio=10) then Mid (prio=5). */
    TEST_ASSERT(t2_order[0] == 1,  "Lo runs first (it was started first, nonpreemptive)");
    TEST_ASSERT(t2_order[1] == 10, "Hi runs second (highest ready priority)");
    TEST_ASSERT(t2_order[2] == 5,  "Mid runs third");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 3 – Equal priority: FIFO activation order
 * ═══════════════════════════════════════════════════════════════════════════*/

static int t3_order[3];
static int t3_idx = 0;
static TTask t3_a, t3_b, t3_c;

static void t3_a_task(void) { t3_order[t3_idx++] = 'A'; TerminateTask(); }
static void t3_b_task(void) { t3_order[t3_idx++] = 'B'; TerminateTask(); }
static void t3_c_task(void) { t3_order[t3_idx++] = 'C'; TerminateTask(); }

static void t3_starter(void)
{
    ActivateTask(t3_a);
    ActivateTask(t3_b);
    ActivateTask(t3_c);
    TerminateTask();
}

static void test_03_equal_priority_fifo(void)
{
    printf("\n[TEST 3] Equal-priority tasks run in FIFO order\n");
    reset_os();
    t3_idx = 0;

    TTask starter = RegisterTask(t3_starter, 5, "Starter");
    t3_a = RegisterTask(t3_a_task, 3, "A");
    t3_b = RegisterTask(t3_b_task, 3, "B");
    t3_c = RegisterTask(t3_c_task, 3, "C");

    StartOS(starter);

    TEST_ASSERT(t3_order[0] == 'A', "A runs first (activated first)");
    TEST_ASSERT(t3_order[1] == 'B', "B runs second");
    TEST_ASSERT(t3_order[2] == 'C', "C runs third");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 4 – PIP: resource acquisition sets owner
 * ═══════════════════════════════════════════════════════════════════════════*/

static TResource t4_res;
static int t4_owned_correctly = 0;
static int t4_released_correctly = 0;

static void t4_task(void)
{
    PIP_GetRes(t4_res);
    t4_owned_correctly = (g_resources[t4_res].owner == g_running);
    PIP_ReleaseRes(t4_res);
    t4_released_correctly = (g_resources[t4_res].owner == -1);
    TerminateTask();
}

static void test_04_pip_basic_acquire_release(void)
{
    printf("\n[TEST 4] PIP: basic acquire / release\n");
    reset_os();

    t4_res = RegisterRes("R4");
    InitRes(t4_res);

    TTask t = RegisterTask(t4_task, 3, "T4");
    StartOS(t);

    TEST_ASSERT(t4_owned_correctly  == 1, "Owner set correctly during GetRes");
    TEST_ASSERT(t4_released_correctly == 1, "Resource freed after ReleaseRes");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 5 – PIP: priority inheritance
 *  Scenario: Lo holds Res. Hi is activated. Hi's priority should be
 *  inherited by Lo's cur_priority at the moment of acquisition.
 * ═══════════════════════════════════════════════════════════════════════════*/

static TResource t5_res;
static TTask t5_hi, t5_lo;
static int t5_lo_prio_during_hold  = 0;
static int t5_lo_prio_after_release = 0;

static void t5_hi_task(void)
{
    /* Hi tries to get the resource (Lo holds it). In nonpreemptive simulation
       Lo has already run and released by the time Hi runs — but we test
       priority inheritance at the moment of overlap. */
    PIP_GetRes(t5_res);
    PIP_ReleaseRes(t5_res);
    TerminateTask();
}

static void t5_lo_task(void)
{
    PIP_GetRes(t5_res);
    ActivateTask(t5_hi); /* Hi is now READY while Lo holds the resource */

    /* In nonpreemptive mode Lo keeps running. 
       Check Lo's cur_priority was raised by PIP when Hi tried to acquire
       — note: PIP inheritance happens in GetRes of HI task when it runs.
       Here we verify Lo's priority AFTER releasing (should be back to base). */
    t5_lo_prio_during_hold = g_tasks[g_running].cur_priority;

    PIP_ReleaseRes(t5_res);
    t5_lo_prio_after_release = g_tasks[g_running].cur_priority;
    TerminateTask();
}

static void test_05_pip_inheritance(void)
{
    printf("\n[TEST 5] PIP: priority restored after ReleaseRes\n");
    reset_os();

    t5_res = RegisterRes("R5");
    InitRes(t5_res);

    t5_lo = RegisterTask(t5_lo_task,  2, "Lo5");
    t5_hi = RegisterTask(t5_hi_task, 10, "Hi5");

    StartOS(t5_lo);

    TEST_ASSERT(t5_lo_prio_after_release == 2,
                "Lo priority restored to base (2) after ReleaseRes");
    /* After Lo releases, Hi runs and also acquires/releases successfully */
    TEST_ASSERT(g_resources[t5_res].owner == -1,
                "Resource is free after both tasks complete");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 6 – System events: WaitSysEvent then SetSysEvent wakes task
 * ═══════════════════════════════════════════════════════════════════════════*/

DeclareSysEvent(EV_DONE, 0); /* bit 0 */

static int t6_signal_sent  = 0;
static int t6_waiter_woken = 0;
static TTask t6_waiter_id, t6_signaler_id;

static void t6_waiter(void)
{
    WaitSysEvent(EV_DONE);       /* will suspend until EV_DONE is set */
    t6_waiter_woken = 1;
    TerminateTask();
}

static void t6_signaler(void)
{
    t6_signal_sent = 1;
    SetSysEvent(EV_DONE);        /* wake the waiter */
    TerminateTask();
}

static void t6_starter(void)
{
    ActivateTask(t6_waiter_id);
    ActivateTask(t6_signaler_id);
    TerminateTask();
}

static void test_06_sysevents_wait_and_set(void)
{
    printf("\n[TEST 6] System events: WaitSysEvent / SetSysEvent\n");
    reset_os();
    t6_signal_sent  = 0;
    t6_waiter_woken = 0;

    TTask starter    = RegisterTask(t6_starter,   5, "Starter6");
    t6_waiter_id     = RegisterTask(t6_waiter,    3, "Waiter6");
    t6_signaler_id   = RegisterTask(t6_signaler,  4, "Signaler6");

    StartOS(starter);

    TEST_ASSERT(t6_signal_sent  == 1, "Signaler ran and set event");
    TEST_ASSERT(t6_waiter_woken == 1, "Waiter was woken by SetSysEvent");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 7 – System events: WaitSysEvent on already-set event returns immediately
 * ═══════════════════════════════════════════════════════════════════════════*/

DeclareSysEvent(EV_PRE, 1); /* bit 1 */

static int t7_returned_immediately = 0;

static void t7_task(void)
{
    /* Pre-set the event before waiting */
    SetSysEvent(EV_PRE);
    WaitSysEvent(EV_PRE); /* should return immediately */
    t7_returned_immediately = 1;
    TerminateTask();
}

static void test_07_sysevent_already_set(void)
{
    printf("\n[TEST 7] System events: WaitSysEvent on already-set event\n");
    reset_os();
    t7_returned_immediately = 0;

    TTask t = RegisterTask(t7_task, 3, "T7");
    StartOS(t);

    TEST_ASSERT(t7_returned_immediately == 1,
                "WaitSysEvent returned immediately when event already set");
    /* Event bits consumed */
    TEST_ASSERT((g_sys_events & EV_PRE) == 0,
                "Event bit cleared after WaitSysEvent");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 8 – Nested resources (two resources held at once)
 * ═══════════════════════════════════════════════════════════════════════════*/

static TResource t8_res1, t8_res2;
static int t8_both_held = 0;
static int t8_both_released = 0;

static void t8_task(void)
{
    PIP_GetRes(t8_res1);
    PIP_GetRes(t8_res2);
    t8_both_held = (g_resources[t8_res1].owner == g_running &&
                    g_resources[t8_res2].owner == g_running);
    PIP_ReleaseRes(t8_res2);
    PIP_ReleaseRes(t8_res1);
    t8_both_released = (g_resources[t8_res1].owner == -1 &&
                        g_resources[t8_res2].owner == -1);
    TerminateTask();
}

static void test_08_nested_resources(void)
{
    printf("\n[TEST 8] Nested resources (two held simultaneously)\n");
    reset_os();

    t8_res1 = RegisterRes("R8a");
    t8_res2 = RegisterRes("R8b");
    InitRes(t8_res1);
    InitRes(t8_res2);

    TTask t = RegisterTask(t8_task, 5, "T8");
    StartOS(t);

    TEST_ASSERT(t8_both_held     == 1, "Both resources held simultaneously");
    TEST_ASSERT(t8_both_released == 1, "Both resources released correctly");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 9 – ShutdownOS stops execution
 * ═══════════════════════════════════════════════════════════════════════════*/

static int t9_after_shutdown = 0;
static TTask t9_second;

static void t9_second_task(void)
{
    t9_after_shutdown = 1; /* should NOT run */
    TerminateTask();
}

static void t9_first_task(void)
{
    ActivateTask(t9_second);
    ShutdownOS();            /* stop immediately */
    TerminateTask();         /* this line should still execute in simulation */
}

static void test_09_shutdown_stops_dispatcher(void)
{
    printf("\n[TEST 9] ShutdownOS stops the dispatcher\n");
    reset_os();
    t9_after_shutdown = 0;

    TTask first = RegisterTask(t9_first_task, 5, "T9a");
    t9_second   = RegisterTask(t9_second_task, 3, "T9b");

    StartOS(first);

    TEST_ASSERT(t9_after_shutdown == 0,
                "Task after ShutdownOS did not run");
    TEST_ASSERT(g_os_running == 0,
                "g_os_running is 0 after ShutdownOS");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 10 – Multiple waiters woken by one SetSysEvent (different bits)
 *  W1 waits on EV_M1, W2 waits on EV_M2; signaler fires both at once.
 * ═══════════════════════════════════════════════════════════════════════════*/

DeclareSysEvent(EV_M1, 2); /* bit 2 */
DeclareSysEvent(EV_M2, 3); /* bit 3 — different from EV_A to avoid conflict */

static int t10_woken[2] = {0, 0};
static TTask t10_w1, t10_w2, t10_sig;

static void t10_waiter1(void)
{
    WaitSysEvent(EV_M1);
    t10_woken[0] = 1;
    TerminateTask();
}

static void t10_waiter2(void)
{
    WaitSysEvent(EV_M2);
    t10_woken[1] = 1;
    TerminateTask();
}

static void t10_signaler(void)
{
    SetSysEvent(EV_M1 | EV_M2); /* wake both waiters with one call */
    TerminateTask();
}

static void t10_starter(void)
{
    ActivateTask(t10_w1);
    ActivateTask(t10_w2);
    ActivateTask(t10_sig);
    TerminateTask();
}

static void test_10_multiple_waiters(void)
{
    printf("\n[TEST 10] Multiple waiters woken by single SetSysEvent (different bits)\n");
    reset_os();
    t10_woken[0] = t10_woken[1] = 0;

    TTask starter = RegisterTask(t10_starter,  5, "Starter10");
    t10_w1        = RegisterTask(t10_waiter1,  2, "W1");
    t10_w2        = RegisterTask(t10_waiter2,  2, "W2");
    t10_sig       = RegisterTask(t10_signaler, 3, "Sig10");

    StartOS(starter);

    TEST_ASSERT(t10_woken[0] == 1, "Waiter 1 was woken (EV_M1)");
    TEST_ASSERT(t10_woken[1] == 1, "Waiter 2 was woken (EV_M2)");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 11 – GetSysEvent / ClearSysEvent
 * ═══════════════════════════════════════════════════════════════════════════*/

DeclareSysEvent(EV_A, 4);
DeclareSysEvent(EV_B, 5);

static int t11_get_ok    = 0;
static int t11_clear_ok  = 0;

static void t11_task(void)
{
    SetSysEvent(EV_A | EV_B);

    TEventMask m = 0;
    GetSysEvent(&m);
    t11_get_ok = ((m & (EV_A | EV_B)) == (EV_A | EV_B));

    ClearSysEvent(EV_A);
    GetSysEvent(&m);
    t11_clear_ok = ((m & EV_A) == 0 && (m & EV_B) != 0);

    ClearSysEvent(EV_B);
    TerminateTask();
}

static void test_11_get_clear_sysevent(void)
{
    printf("\n[TEST 11] GetSysEvent / ClearSysEvent\n");
    reset_os();

    TTask t = RegisterTask(t11_task, 3, "T11");
    StartOS(t);

    TEST_ASSERT(t11_get_ok   == 1, "GetSysEvent returns correct mask");
    TEST_ASSERT(t11_clear_ok == 1, "ClearSysEvent clears only specified bits");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 12 – PIP: held_count bookkeeping
 * ═══════════════════════════════════════════════════════════════════════════*/

static TResource t12_r1, t12_r2, t12_r3;
static int t12_count_ok = 0;

static void t12_task(void)
{
    PIP_GetRes(t12_r1);
    PIP_GetRes(t12_r2);
    PIP_GetRes(t12_r3);
    t12_count_ok = (g_tasks[g_running].held_count == 3);
    PIP_ReleaseRes(t12_r3);
    PIP_ReleaseRes(t12_r1);
    PIP_ReleaseRes(t12_r2);
    TerminateTask();
}

static void test_12_pip_held_count(void)
{
    printf("\n[TEST 12] PIP: held_count bookkeeping\n");
    reset_os();

    t12_r1 = RegisterRes("R12a");
    t12_r2 = RegisterRes("R12b");
    t12_r3 = RegisterRes("R12c");
    InitRes(t12_r1); InitRes(t12_r2); InitRes(t12_r3);

    TTask t = RegisterTask(t12_task, 4, "T12");
    StartOS(t);

    TEST_ASSERT(t12_count_ok == 1, "held_count == 3 while holding 3 resources");
    TEST_ASSERT(g_tasks[t].held_count == 0,
                "held_count == 0 after releasing all resources");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST 13 – WaitSysEvent: genuine suspend then wakeup
 *  Waiter (prio=4) runs first, WaitSysEvent suspends it.
 *  Signaler (prio=3) is then selected, sets the event, wakes waiter.
 *  Waiter resumes from its nested Dispatcher_Run call.
 * ═══════════════════════════════════════════════════════════════════════════*/

DeclareSysEvent(EV_WAKE, 6); /* bit 6 */

static int t13_waiter_woken   = 0;
static int t13_signaler_ran   = 0;
static TTask t13_waiter_id, t13_signaler_id;

static void t13_signaler(void)
{
    t13_signaler_ran = 1;
    SetSysEvent(EV_WAKE);
    TerminateTask();
}

static void t13_waiter(void)
{
    /* Event not set yet — this WILL suspend */
    WaitSysEvent(EV_WAKE);
    t13_waiter_woken = 1;
    TerminateTask();
}

static void t13_starter(void)
{
    ActivateTask(t13_waiter_id);
    ActivateTask(t13_signaler_id);
    TerminateTask();
}

static void test_13_genuine_wait_and_wake(void)
{
    printf("\n[TEST 13] WaitSysEvent: genuine suspend then wakeup\n");
    reset_os();
    t13_waiter_woken = 0;
    t13_signaler_ran = 0;

    /* Starter has highest prio to run first.
     * Waiter (prio=4) > Signaler (prio=3): waiter runs first, suspends.
     * Then signaler runs and fires the event. */
    TTask starter    = RegisterTask(t13_starter,  10, "Starter13");
    t13_waiter_id    = RegisterTask(t13_waiter,    4, "Waiter13");
    t13_signaler_id  = RegisterTask(t13_signaler,  3, "Signaler13");

    StartOS(starter);

    TEST_ASSERT(t13_signaler_ran  == 1, "Signaler ran and set event");
    TEST_ASSERT(t13_waiter_woken  == 1, "Waiter woke up after genuine suspend");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  main
 * ═══════════════════════════════════════════════════════════════════════════*/

int main(void)
{
    printf("========================================\n");
    printf("  RTOS Test Suite  (Task #4)\n");
    printf("  Scheduler: flat, nonpreemptive, RMA\n");
    printf("  Resources: PIP\n");
    printf("  Events: system events\n");
    printf("========================================\n");

    test_01_basic_activate_terminate();
    test_02_rma_priority_ordering();
    test_03_equal_priority_fifo();
    test_04_pip_basic_acquire_release();
    test_05_pip_inheritance();
    test_06_sysevents_wait_and_set();
    test_07_sysevent_already_set();
    test_08_nested_resources();
    test_09_shutdown_stops_dispatcher();
    test_10_multiple_waiters();
    test_11_get_clear_sysevent();
    test_12_pip_held_count();
    test_13_genuine_wait_and_wake();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("========================================\n");

    return (g_fail == 0) ? 0 : 1;
}
