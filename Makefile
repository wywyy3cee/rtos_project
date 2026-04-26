# Makefile for RTOS project (Task #4)
# Targets: rtos_tests (Linux/macOS), rtos_tests.exe (Windows via MinGW)

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -pedantic -DRTOS_DEBUG
SRCS     = src/global.c src/os.c src/task.c src/resource.c
TEST_SRC = tests/tests.c
APP_SRC  = app/main.c  
INC      = -Isrc

.PHONY: all clean test

all: rtos_tests my_app 

# Сборка тестов
rtos_tests: $(SRCS) $(TEST_SRC)
	$(CC) $(CFLAGS) $(INC) $^ -o $@

my_app: $(SRCS) $(APP_SRC)
	$(CC) $(CFLAGS) $(INC) $^ -o $@

# Release build (no debug output)
release:
	$(CC) -std=c99 -O2 -Wall $(INC) $(SRCS) $(TEST_SRC) -o rtos_tests
	$(CC) -std=c99 -O2 -Wall $(INC) $(SRCS) $(APP_SRC) -o my_app    

test: rtos_tests
	./rtos_tests

# Windows (MinGW)
win:
	gcc -std=c99 -Wall -Wextra -DRTOS_DEBUG $(INC) $(SRCS) $(TEST_SRC) -o rtos_tests.exe
	gcc -std=c99 -Wall -Wextra -DRTOS_DEBUG $(INC) $(SRCS) $(APP_SRC) -o my_app.exe     

# <-- ИЗМЕНЕНО: добавлено удаление файлов приложения (my_app и my_app.exe)
clean:
	rm -f rtos_tests rtos_tests.exe my_app my_app.exe
