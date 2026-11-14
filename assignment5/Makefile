CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread

file_manager: file_manager.c
	$(CC) $(CFLAGS) file_manager.c -o file_manager

.PHONY: test clean
test: file_manager
	python3 test_project.py
clean:
	rm -f file_manager
