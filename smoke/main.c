// Runs the smoke test's native image: smoke <writable directory>
#include <stdio.h>
#include "smoke.h"

int main(int argc, char **argv) {
    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;
    if (argc < 2 || graal_create_isolate(NULL, &isolate, &thread) != 0) {
        fprintf(stderr, "smoke: no isolate\n");
        return 1;
    }
    return smoke_run(thread, argv[1]);
}
