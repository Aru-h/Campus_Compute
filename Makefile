# Makefile for CampusCompute
#
#   make          build the program  (creates ./campuscompute)
#   make run      build and run the integrated demo
#   make test     build and run the automatic checks (tests/run_tests.sh)
#   make clean    remove build files and the log file

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g -D_GNU_SOURCE
LDLIBS  = -pthread -lrt

SOURCES = main.c util.c jobs.c sched.c logger.c ipc.c banker.c worker.c limits.c verify.c demo.c
OBJECTS = $(SOURCES:.c=.o)
TARGET  = campuscompute

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

# every .o file is rebuilt if its .c file or the shared header changes
%.o: %.c campuscompute.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) demo

test: $(TARGET)
	sh tests/run_tests.sh

clean:
	rm -f $(OBJECTS) $(TARGET) campuscompute.log

.PHONY: all run test clean
