#include "../osmpLibrary/OSMP.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MESSAGES_TO_SEND 100

int main(int argc, char **argv) {
    if (OSMP_Init(&argc, &argv) != OSMP_SUCCESS) {
        fprintf(stderr, "OSMP_Init failed\n");
        return EXIT_FAILURE;
    }

    int rank, size;
    OSMP_Rank(&rank);
    OSMP_Size(&size);

    if (size < 2) {
        fprintf(stderr, "Need at least 2 peers for circular stress test\n");
        OSMP_Finalize();
        return EXIT_FAILURE;
    }

    int right_neighbor = (rank + 1) % size;
    int left_neighbor = (rank - 1 + size) % size;

    OSMP_Request reqs[MESSAGES_TO_SEND];
    int send_data[MESSAGES_TO_SEND];

    fprintf(stdout, "[Peer %d] Starting circular stress test...\n", rank);

    // 1. Post all ISends to the right neighbor
    for (int i = 0; i < MESSAGES_TO_SEND; i++) {
        send_data[i] = rank * 1000 + i;
        if (OSMP_CreateRequest(&reqs[i]) != OSMP_SUCCESS) {
            fprintf(stderr, "[Peer %d] Failed to create request\n", rank);
            return EXIT_FAILURE;
        }
        if (OSMP_ISend(&send_data[i], 1, OSMP_INT, right_neighbor, reqs[i]) != OSMP_SUCCESS) {
            fprintf(stderr, "[Peer %d] ISend failed\n", rank);
            return EXIT_FAILURE;
        }
    }

    // 2. Receive all messages from the left neighbor synchronously
    // This MUST not deadlock. If we used OSMP_Send, we'd be stuck there because the mailbox might be full.
    for (int i = 0; i < MESSAGES_TO_SEND; i++) {
        int recv_val;
        int source, len;
        if (OSMP_Recv(&recv_val, 1, OSMP_INT, &source, &len) != OSMP_SUCCESS) {
            fprintf(stderr, "[Peer %d] Recv failed\n", rank);
            return EXIT_FAILURE;
        }
        if (source != left_neighbor) {
            fprintf(stderr, "[Peer %d] Received from wrong source %d (expected %d)\n", rank, source, left_neighbor);
        }
    }

    // 3. Wait for all ISends to complete
    for (int i = 0; i < MESSAGES_TO_SEND; i++) {
        if (OSMP_Wait(reqs[i]) != OSMP_SUCCESS) {
            fprintf(stderr, "[Peer %d] Wait failed\n", rank);
        }
        OSMP_RemoveRequest(&reqs[i]);
    }

    fprintf(stdout, "[Peer %d] Circular stress test completed successfully.\n", rank);
    OSMP_Barrier();
    OSMP_Finalize();
    return EXIT_SUCCESS;
}
