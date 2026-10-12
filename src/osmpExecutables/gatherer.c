#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../osmpLibrary/OSMP.h"

int main(int argc, char **argv) {
    if (OSMP_SUCCESS != OSMP_Init(&argc, &argv)) {
        fprintf(stderr, "Fehler: OSMP_Init fehlgeschlagen\n");
        return EXIT_FAILURE;
    }

    int rank, size;
    if (OSMP_Rank(&rank) != OSMP_SUCCESS) {
        if (OSMP_SUCCESS != OSMP_Finalize()) {
            fprintf(stderr, "Fehler: OSMP_Finalize\n");
            return EXIT_FAILURE;
        }
        return -1;
    }

    if (OSMP_Size(&size) != OSMP_SUCCESS) {
        if (OSMP_SUCCESS != OSMP_Finalize()) {
            fprintf(stderr, "Fehler: OSMP_Finalize\n");
            return EXIT_FAILURE;
        }
        return -1;
    }

    int send_array[20];
    int* recv_buf = NULL;
    int recv_size = 0, root = 0;

    if (rank == root) {
        recv_size = size * 20; //TODO dynamic later
        fprintf(stdout, "Total buffer size is: %d\n", recv_size);
        recv_buf = malloc((u_long)recv_size * sizeof(int));
        OSMP_Log(OSMP_LOG_ALL, "[R_PEER]: Now gathering messages from all peers\n");
    }

    // init message
    for (int i = 0; i < 20; i++) {
        send_array[i] = rank;
    }

    if (OSMP_SUCCESS != OSMP_Gather(send_array, 20, OSMP_INT, recv_buf, recv_size, OSMP_INT, root)) {
        OSMP_Log(OSMP_LOG_ALL, "[R_PEER]: Failed to gather messages\n");
    }
    if (rank == root) {
        for (int i = 0; i < recv_size; i++) {
            if (i%20 != 0 ){
                fprintf(stdout, " %d ", recv_buf[i]);
            }
            else {
                fprintf(stdout, "\n");
            }
        }
    }


    if (recv_buf != NULL) free(recv_buf);

    if (OSMP_SUCCESS != OSMP_Finalize()) {
        fprintf(stderr, "Fehler: OSMP_Finalize\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}