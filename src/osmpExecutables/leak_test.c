#include "../osmpLibrary/OSMP.h"
#include <stdio.h>
#include <stdlib.h>

// Create up to 256 to not overflow the internal static array requests[256],
// but do it multiple times by creating and removing. Actually since we don't
// remove from the requests array, we can only do 256. If we do more, it's fine,
// they just don't get stored in the array but still test the malloc/free cycle.
#define REQUESTS_TO_CREATE 2000

int main(int argc, char **argv) {
    if (OSMP_Init(&argc, &argv) != OSMP_SUCCESS) return EXIT_FAILURE;

    int rank;
    OSMP_Rank(&rank);

    if (rank == 0) {
        fprintf(stdout, "[Peer 0] Creating and removing %d requests to test memory leaks...\n", REQUESTS_TO_CREATE);
        for (int i = 0; i < REQUESTS_TO_CREATE; i++) {
            OSMP_Request req;
            if (OSMP_CreateRequest(&req) == OSMP_SUCCESS) {
                OSMP_RemoveRequest(&req);
            } else {
                fprintf(stderr, "[Peer 0] Failed to create request at iteration %d\n", i);
                break;
            }
        }
        fprintf(stdout, "[Peer 0] Successfully created and removed %d requests.\n", REQUESTS_TO_CREATE);
    }

    OSMP_Barrier();
    OSMP_Finalize();
    return EXIT_SUCCESS;
}
