/*
 * Saved data on the web (bronto/save.h): counts how many times it has run,
 * in a saved file. tools/web/test/web.test.js runs it three times with the
 * same browser storage; each run must find the last one's file. It also
 * checks that a name reaching outside the game's folder is refused.
 */
#include <stdio.h>
#include <stdlib.h>
#include "bronto/memory.h"
#include "bronto/save.h"

int main(void) {
    size_t size;
    char *text, out[32];
    int runs, n;
    br_save_set_game("Persist Test");
    if (br_save_name_ok("../escape.txt")) return 2;
    text = (char *)br_save_read("runs.txt", &size);
    runs = text ? atoi(text) : 0;
    br_free(text);
    n = snprintf(out, sizeof(out), "%d\n", runs + 1);
    if (!br_save_write("runs.txt", out, (size_t)n)) return 1;
    printf("run %d\n", runs + 1);
    return 0;
}
