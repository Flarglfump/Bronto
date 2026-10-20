/*
 * Saved files on the web: counts how many times it has run, in a file in a
 * folder. tools/web/test/web.test.js runs it twice with the same browser
 * storage; the second run must find the first one's file.
 */
#include <stdio.h>
#include <stdlib.h>
#include "bronto/fs.h"
#include "bronto/memory.h"

int main(void) {
    size_t size;
    char *text, out[32];
    int runs, n;
    br_fs_mkdir("saves");
    text = (char *)br_file_read_all("saves/runs.txt", &size);
    runs = text ? atoi(text) : 0;
    br_free(text);
    n = snprintf(out, sizeof(out), "%d\n", runs + 1);
    if (!br_file_write_all("saves/runs.txt", out, (size_t)n)) return 1;
    printf("run %d\n", runs + 1);
    return 0;
}
