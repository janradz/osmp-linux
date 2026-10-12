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
    OSMP_Rank(&rank);
    OSMP_Size(&size);

    OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: Now sending message to all peers\n");
    for (int i = 0; i < size; i++) {
        if (i != rank) {
            int test_msg[1] = {rank};
            OSMP_Send(&test_msg, 1, OSMP_INT, i);
        }
    }
    OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: Now Reading messages\n");
    char buf[OSMP_MAX_PAYLOAD_LENGTH];
    int len;
    int sender;
    for (int received = 0; received < size - 1; received++) {
        if (OSMP_Recv(&buf, OSMP_MAX_PAYLOAD_LENGTH, OSMP_INT, &sender, &len) != OSMP_SUCCESS) {
            fprintf(stderr, "Fehler: OSMP_Recv fehlgeschlagen\n");
            OSMP_Finalize();
            return EXIT_FAILURE;
        }

        int temp_msg[len];
        memcpy(temp_msg, buf, (size_t)len);
        fprintf(stdout, "Received Msg from %d, length: %d: %d\n", sender, len, temp_msg[0]);
    }

    if (OSMP_SUCCESS != OSMP_Finalize()) {
        fprintf(stderr, "Fehler: OSMP_Finalize\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}