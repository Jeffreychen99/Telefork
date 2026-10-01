#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* No sockets, mutable files, or terminal: state lives in the process heap.
 * Inspect progress with a debugger: p *counter
 */
volatile unsigned long *counter;

int main(void) {
    counter = malloc(sizeof(*counter));
    if (counter == NULL) return 1;
    *counter = 1000000;
    fprintf(stderr, "counter pid=%ld address=%p initial=%lu\n",
            (long)getpid(), (void *)counter, *counter);
    if (freopen("/dev/null", "r", stdin) == NULL ||
        freopen("/dev/null", "w", stdout) == NULL ||
        freopen("/dev/null", "w", stderr) == NULL) return 1;
    for (;;) {
        struct timespec remaining = {.tv_sec = 1, .tv_nsec = 0};
        while (nanosleep(&remaining, &remaining) != 0) {}
        ++*counter;
    }
}
