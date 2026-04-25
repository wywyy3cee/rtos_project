#include <stdio.h>
#include "rtos_api.h"

// Объявляем два системных события (0-й и 1-й биты)
DeclareSysEvent(EV_PING, 0);
DeclareSysEvent(EV_PONG, 1);

// Глобальные идентификаторы задач
TTask t_ping, t_pong;

// --- Задача PING (Низкий приоритет: 2) ---
static void ping_task(void) {
    printf("[Ping] Работает! Отправляю событие EV_PING...\n");
    SetSysEvent(EV_PING);
    
    printf("[Ping] Ухожу в ожидание ответа (EV_PONG)...\n");
    // Планировщик заберет управление и отдаст его задаче Pong
    WaitSysEvent(EV_PONG); 
    
    printf("[Ping] Ответ (EV_PONG) получен! Завершаю работу.\n");
    TerminateTask();
}

// --- Задача PONG (Высокий приоритет: 3) ---
static void pong_task(void) {
    printf("[Pong] Запущена, но жду сигнала от Ping (EV_PING)...\n");
    // Сразу уходим в спящий режим, пока Ping не пришлет событие
    WaitSysEvent(EV_PING);
    
    printf("[Pong] Сигнал получен! Отправляю ответ (EV_PONG)...\n");
    SetSysEvent(EV_PONG);
    
    TerminateTask();
}

// --- Стартовая задача (Самый высокий приоритет: 10) ---
static void starter_task(void) {
    printf("[Starter] Активирую задачи...\n");
    // Активируем обе задачи. Они переходят в состояние READY.
    ActivateTask(t_pong);
    ActivateTask(t_ping);
    
    printf("[Starter] Задачи активированы. Завершаю стартер.\n");
    // После завершения стартера, планировщик выберет самую приоритетную READY-задачу (Pong).
    TerminateTask();
}

int main(void) {
    printf("=== Запуск пользовательского приложения на базе ОСРВ ===\n\n");
    
    // 1. Регистрация задач в ОС
    TTask starter = RegisterTask(starter_task, 10, "Starter");
    t_pong        = RegisterTask(pong_task, 3, "Pong");
    t_ping        = RegisterTask(ping_task, 2, "Ping");

    // 2. Передача управления планировщику
    StartOS(starter);
    
    printf("\n=== ОС успешно завершила работу ===\n");
    return 0;
}