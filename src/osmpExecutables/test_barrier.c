#include <stdio.h>
#include <unistd.h> //
#include "../osmpLibrary/OSMP.h"

int main(int argc, char **argv) {
    OSMP_Init(&argc, &argv);

    int rank;
    OSMP_Rank(&rank);

    //test von 2 cycles
    for (int cycle = 1; cycle <= 200; cycle++) {

        // asymmetrie: peers kommen später an
        //usleep((useconds_t)(rank + 1) * 200000);


        printf("[Cycle %d] Peer %d: WARTET...\n", cycle, rank);
        fflush(stdout);

        // BARRIER TEST:
        OSMP_Barrier();


        printf("[Cycle %d] Peer %d: PASSIERT!\n", cycle, rank);
        fflush(stdout);
    }

    OSMP_Finalize();
    return 0;
}