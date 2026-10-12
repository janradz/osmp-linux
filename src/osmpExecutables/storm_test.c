#include "../osmpLibrary/OSMP.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MESSAGES_PER_PEER 50

int main(int argc, char **argv) {
    if (OSMP_Init(&argc, &argv) != OSMP_SUCCESS) {
        return EXIT_FAILURE;
    }

    int rank, size;
    OSMP_Rank(&rank);
    OSMP_Size(&size);

    if (size < 2) {
        OSMP_Finalize();
        return EXIT_FAILURE;
    }

    if (rank != 0) {
        OSMP_Request reqs[MESSAGES_PER_PEER];
        int data[MESSAGES_PER_PEER];
        
        for (int i = 0; i < MESSAGES_PER_PEER; i++) {
            data[i] = rank;
            OSMP_CreateRequest(&reqs[i]);
            OSMP_ISend(&data[i], 1, OSMP_INT, 0, reqs[i]);
        }
        
        for (int i = 0; i < MESSAGES_PER_PEER; i++) {
            OSMP_Wait(reqs[i]);
            OSMP_RemoveRequest(&reqs[i]);
        }
        fprintf(stdout, "[Peer %d] Finished sending %d messages to Peer 0\n", rank, MESSAGES_PER_PEER);
    } else {
        int expected_total = (size - 1) * MESSAGES_PER_PEER;
        int received_counts[256] = {0};
        
        fprintf(stdout, "[Peer 0] Waiting for %d total messages in storm test...\n", expected_total);

        for (int i = 0; i < expected_total; i++) {
            int recv_val, source, len;
            if (OSMP_Recv(&recv_val, 1, OSMP_INT, &source, &len) == OSMP_SUCCESS) {
                received_counts[source]++;
            } else {
                fprintf(stderr, "[Peer 0] OSMP_Recv failed\n");
            }
        }
        
        int success = 1;
        for (int p = 1; p < size; p++) {
            if (received_counts[p] != MESSAGES_PER_PEER) {
                fprintf(stderr, "[Peer 0] Error: Expected %d from Peer %d, got %d\n", MESSAGES_PER_PEER, p, received_counts[p]);
                success = 0;
            }
        }
        if (success) {
            fprintf(stdout, "[Peer 0] Successfully received all %d messages from all peers!\n", expected_total);
        }
    }

    OSMP_Barrier();
    OSMP_Finalize();
    return EXIT_SUCCESS;
}
