/*
 * portTodo: the PORT_TODO marker of include/common.h. Game code that the PC
 * must do differently calls it until the PC version exists; each place is
 * reported once.
 */

#include <stdio.h>
#include <string.h>

#define MAX_REPORTED 512

static const char* reported[MAX_REPORTED];
static int reportedCount;

void portTodo(const char* func, const char* why) {
    for (int i = 0; i < reportedCount; i++) {
        if (strcmp(reported[i], func) == 0) {
            return;
        }
    }
    if (reportedCount < MAX_REPORTED) {
        reported[reportedCount++] = func;
    }
    fprintf(stderr, "PORT_TODO %s: %s\n", func, why);
}

/* The generated stubs of tools/pc_link.py: a function the PC does not have yet. */
void portStub(const char* name) {
    portTodo(name, "stub: not on the PC yet");
}
