#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "../osmpLibrary/OSMP.h"

int main(int argc, char **argv) {
    if (OSMP_SUCCESS != OSMP_Init(&argc, &argv)) {
        fprintf(stderr, "Fehler: OSMP_Init fehlgeschlagen\n");
        return EXIT_FAILURE;
    }

    int rank, size;
    OSMP_Rank(&rank);
    OSMP_Size(&size);

    OSMP_Request requests[(size*2)-1];
    int test_msg[1] = {rank};
    OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: Now sending message to all peers\n");
    int send_count = 0;
    for (int i = 0; i < size; i++) {
        if (i != rank) {
            OSMP_Request send_request;
            if (OSMP_SUCCESS != OSMP_CreateRequest(&send_request)) {
                fprintf(stderr, "Fehler: OSMP_CreateRequest fehlgeschlagen\n");
                OSMP_Finalize();
                return EXIT_FAILURE;
            }
            requests[send_count] = send_request;

            if (OSMP_SUCCESS != OSMP_ISend(&test_msg, 1, OSMP_INT, i, send_request)) {
                OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: ISend Failure");
            }
            send_count++;
        }
    }
    int status;
    for (int i = 0; i < size-1; i++) {
        OSMP_Test(requests[i], &status);
        fprintf(stderr, "Request %d, Status %d\n", i, status);
    }

    if (OSMP_SUCCESS != OSMP_Finalize()) {
        fprintf(stderr, "Fehler: OSMP_Finalize\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < send_count * 2; i++) {
        OSMP_Test(requests[i], &status);
        fprintf(stderr, "Request %d, Status %d\n", i, status);
    }




    return EXIT_SUCCESS;
}