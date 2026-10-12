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
            char test_msg[100] = {'A'};
            memset(test_msg, 'A' + rank, sizeof(test_msg));
            OSMP_Send(&test_msg, 100, OSMP_BYTE, i);
        }
    }
    OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: Now Reading messages\n");
    char buf[OSMP_MAX_PAYLOAD_LENGTH];
    int len;
    int sender;
    for (int received = 0; received < size - 1; received++) {
        if (OSMP_Recv(&buf, OSMP_MAX_PAYLOAD_LENGTH, OSMP_BYTE, &sender, &len) != OSMP_SUCCESS) {
            fprintf(stderr, "Fehler: OSMP_Recv fehlgeschlagen\n");
            OSMP_Finalize();
            return EXIT_FAILURE;
        }

        char temp_msg[len + 1];
        memcpy(temp_msg, buf, (size_t)len);
        temp_msg[len] = '\0';
        fprintf(stdout, "Received Msg from %d, length: %d: %s\n", sender, len, temp_msg);
    }

    if (OSMP_SUCCESS != OSMP_Finalize()) {
        fprintf(stderr, "Fehler: OSMP_Finalize\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}