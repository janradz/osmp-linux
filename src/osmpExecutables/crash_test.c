#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    // Dieser Test überprüft das TimedWait-Verhalten des Runners.
    // Wir initialisieren ABSICHTLICH NICHT OSMP_Init(), sodass der Runner
    // im sem_timedwait auf uns warten muss.
    
    fprintf(stdout, "[CRASH TEST] Peer started. Simulating a delay...\n");
    
    // Wir schlafen für 2 Sekunden. Der Runner hat einen 1-Sekunden-Timeout.
    // Das bedeutet: 
    // - Nach 1s gibt es einen Timeout im Runner. Der Runner checkt waitpid().
    //   Wir sind noch am Leben, also macht er "continue" und wartet noch eine Sekunde.
    sleep(2);
    
    fprintf(stdout, "[CRASH TEST] Delay finished. Simulating a hard crash (Segfault)...\n");
    
    // Nach den 2 Sekunden provozieren wir einen harten Crash (Segfault).
    // - Nach einer weiteren Sekunde im Runner gibt es wieder einen Timeout.
    //   Diesmal checkt der Runner waitpid() und sieht, dass wir gestorben sind.
    //   Der Runner bricht alles geordnet ab!
    
    int *ptr = NULL;
    *ptr = 42; // Segmentation Fault
    
    return 0;
}
