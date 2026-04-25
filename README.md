# ОСРВ -- Операционная система реального времени

## Свойства реализованной ОС

| Свойство                   | Значение                            |
|----------------------------|-------------------------------------|
| Тип планировщика           | Плоский (flat)                      |
| Алгоритм планирования      | Невытесняющий (nonpreemptive), RMA  |
| Управление ресурсами       | PIP (Priority Inheritance Protocol) |
| Управление событиями       | Системные события                   |
| Обработка прерываний       | Нет                                 |
| Макс. задач                | 32                                  |
| Макс. ресурсов             | 16                                  |
| Макс. событий              | 16 (бит-маска uint32_t)             |

---

## Структура проекта

```
rtos_project/
├── src/
│   ├── rtos_api.h    — публичный API (подключается пользовательскими приложениями)
│   ├── sys.h         — внутренние структуры данных (TCB, RCB, глобальные extern)
│   ├── global.c      — определения всех глобальных переменных
│   ├── os.c          — запуск/останов ОС, системные события
│   ├── task.c        — управление задачами, планировщик, диспетчер
│   └── resource.c    — управление ресурсами (PIP)
├── tests/
│   └── tests.c       — набор из 13 тестов
├── Makefile
└── README.md
```

---

## Сборка и запуск

### Linux / macOS

```bash
# Сборка (с отладочным выводом)
make

# Запуск тестов
make test

# Сборка без отладочного вывода
make release
./rtos_tests
```

### Windows (MinGW / MSYS2)

```bash
# Установить MinGW: https://www.mingw-w64.org/
# Затем в командной строке:
make win
rtos_tests.exe
```

Или напрямую без make:

```bash
gcc -std=c99 -Wall -DRTOS_DEBUG -Isrc ^
    src/global.c src/os.c src/task.c src/resource.c ^
    tests/tests.c -o rtos_tests.exe
rtos_tests.exe
```

### Флаги компилятора

| Флаг          | Назначение                              |
|---------------|-----------------------------------------|
| `-DRTOS_DEBUG`| Включить отладочный вывод `[RTOS] ...`  |
| `-std=c99`    | Стандарт ISO C99                        |

---

## API — краткий справочник

### Типы данных

```c
typedef int      TTask;       // идентификатор задачи
typedef int      TResource;   // идентификатор ресурса
typedef uint32_t TEventMask;  // маска событий (до 32 бит)
```

### Макросы

```c
// Объявление задачи (до использования)
DeclareTask(myTask);

// Определение задачи с приоритетом
TASK(myTask, 5) {
    // тело задачи
    TerminateTask();
}

// Объявление ресурса
DeclareResource(myRes);

// Объявление системного события (bit = 0..15)
DeclareSysEvent(EV_READY, 0);  // создаёт константу EV_READY = (1u << 0)
```

### Регистрация (вызывать до StartOS)

```c
TTask     RegisterTask(void (*func)(void), int priority, const char *name);
TResource RegisterRes(const char *name);
```

### Управление задачами

```c
void ActivateTask(TTask taskId);  // suspended → ready
void TerminateTask(void);         // running → suspended (последний вызов в задаче)
```

### Управление ресурсами (PIP)

```c
void InitRes(TResource resId);        // инициализация ресурса
void PIP_GetRes(TResource resId);     // захват (вход в критическую секцию)
void PIP_ReleaseRes(TResource resId); // освобождение (выход из критической секции)
```

### Системные события

```c
void SetSysEvent(TEventMask mask);          // установить биты событий
void ClearSysEvent(TEventMask mask);        // сбросить биты событий
void GetSysEvent(TEventMask *mask);         // прочитать текущую маску
void WaitSysEvent(TEventMask mask);         // ждать хотя бы одного события из маски
```

### Жизненный цикл ОС

```c
void StartOS(TTask firstTask);  // инициализация и запуск; возвращает после ShutdownOS
void ShutdownOS(void);          // немедленная остановка ОС
```

---

## Пример пользовательского приложения

```c
#include "rtos_api.h"

DeclareTask(producer);
DeclareTask(consumer);
DeclareResource(sharedBuf);

DeclareSysEvent(EV_DATA_READY, 0);

static int buffer = 0;
static TResource bufRes;

TASK(producer, 3) {
    PIP_GetRes(bufRes);
    buffer = 42;
    PIP_ReleaseRes(bufRes);
    SetSysEvent(EV_DATA_READY);
    TerminateTask();
}

TASK(consumer, 2) {
    WaitSysEvent(EV_DATA_READY);
    PIP_GetRes(bufRes);
    /* использовать buffer */
    PIP_ReleaseRes(bufRes);
    TerminateTask();
}

int main(void) {
    TTask prod = RegisterTask(producer_func, producer_priority, "producer");
    TTask cons = RegisterTask(consumer_func, consumer_priority, "consumer");
    bufRes = RegisterRes("sharedBuf");
    InitRes(bufRes);

    ActivateTask(cons);   /* ждёт событие */
    StartOS(prod);        /* prod активируется первым */

    ShutdownOS();
    return 0;
}
```

---

## Описание реализации

### Планировщик (task.c — `Scheduler_SelectNext`)

Выбирает из очереди задачу в состоянии READY с наибольшим `base_priority`.
При равных приоритетах побеждает задача с меньшим индексом в массиве TCB
(т.е. задача, зарегистрированная раньше), что обеспечивает FIFO-порядок
для равноприоритетных задач.

### Диспетчер (task.c — `Dispatcher_Run`)

Невытесняющий: запускает одну задачу до её завершения (`TerminateTask`) или
добровольной приостановки (`WaitSysEvent`). После этого выбирает следующую.

Рекурсивный вызов `Dispatcher_Run` из `WaitSysEvent` реализует
«псевдо-вытеснение» в рамках программной симуляции: приостановленная задача
остаётся в стеке вызовов C, пока диспетчер обрабатывает другие задачи.

### PIP (resource.c)

При захвате ресурса (`PIP_GetRes`) сохраняется текущий приоритет задачи в
`RCB.saved_priority`. Если к уже захваченному ресурсу обращается задача с
более высоким приоритетом, приоритет владельца повышается.

При освобождении (`PIP_ReleaseRes`) приоритет восстанавливается как максимум
из `saved_priority` и приоритетов остальных удерживаемых ресурсов.

### Системные события (os.c)

`SetSysEvent` атомарно (в рамках симуляции) переводит всех ждущих задач
(WAITING) в состояние READY и очищает соответствующие биты из `g_sys_events`,
не затрагивая биты, которые ещё ожидают другие задачи.

`WaitSysEvent` проверяет три случая:
1. Задача была разбужена через `SetSysEvent` (`woken_by_event` флаг) → немедленный возврат.
2. Событие уже установлено в `g_sys_events` → очистить и вернуть.
3. Событие не установлено → перейти в WAITING, уступить диспетчеру.

---

## Тесты (tests/tests.c)

| №  | Описание                                                     |
|----|--------------------------------------------------------------|
| 1  | Базовая активация и завершение задачи                        |
| 2  | RMA: задача с высшим приоритетом выполняется первой          |
| 3  | Равный приоритет: FIFO-порядок активации                     |
| 4  | PIP: захват и освобождение ресурса                           |
| 5  | PIP: приоритет восстанавливается после ReleaseRes            |
| 6  | Системные события: WaitSysEvent → SetSysEvent → пробуждение  |
| 7  | Системные события: WaitSysEvent на уже установленное         |
| 8  | Вложенные ресурсы (два одновременно)                         |
| 9  | ShutdownOS останавливает диспетчер                           |
| 10 | Несколько ждущих задач будятся одним SetSysEvent             |
| 11 | GetSysEvent / ClearSysEvent                                  |
| 12 | held_count корректно отслеживается при PIP                   |
| 13 | WaitSysEvent: реальная приостановка и пробуждение            |

---

## Разделение работы (для отчётов)

| Участник | Зона ответственности                                        |
|----------|-------------------------------------------------------------|
| Андрей   | Реализация системных событий (`os.c`, тесты 6–7, 10–11, 13) |
| Егор     | Реализация PIP (`resource.c`, тесты 4–5, 8, 12)             |
| Я (вы)   | Ядро ОС: планировщик, диспетчер, тесты 1–3, 9; интеграция   |
