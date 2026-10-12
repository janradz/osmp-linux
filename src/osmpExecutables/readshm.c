#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../osmpLibrary/OSMP.h"
#include "../osmpLibrary/osmpLib.h"

int main(int argc, char **argv) {
    if (OSMP_SUCCESS != OSMP_Init(&argc, &argv)) {
        fprintf(stderr, "Fehler: OSMP_Init fehlgeschlagen\n");
        return EXIT_FAILURE;
    }

    int rank, size;
    OSMP_Rank(&rank);
    OSMP_Size(&size);

    char shm_name[OSMP_MAX_SHM_NAME_LENGTH];
    if (OSMP_GetShmName(shm_name) != OSMP_SUCCESS) {
        fprintf(stderr, "Fehler beim Abrufen des SHM-Namens.\n");
        OSMP_Finalize();
        return EXIT_FAILURE;
    }

    int fd = shm_open(shm_name, O_RDONLY, 0666);
    if (fd == -1) {
        perror("shm_open in Executable fehlgeschlagen");
        OSMP_Finalize();
        return EXIT_FAILURE;
    }

    struct stat shm_stat;
    if (fstat(fd, &shm_stat) == -1) {
        perror("fstat fehlgeschlagen");
        close(fd);
        OSMP_Finalize();
        return EXIT_FAILURE;
    }


    shm_layout_t *shm_ptr = mmap(NULL, (size_t)shm_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap in Executable fehlgeschlagen");
        close(fd);
        OSMP_Finalize();
        return EXIT_FAILURE;
    }

    printf("\n====================================================\n");
    printf(" OSMP Peer %d von %d liest aus Shared Memory:\n", rank, size);
    printf(" SHM-Name: \t %s\n", shm_name);
    printf(" Process Count: \t %d\n", shm_ptr->num_peers);
    printf(" Verbosity: \t %d\n", shm_ptr->verbosity);
    printf(" Logfile: \t %s\n", shm_ptr->log_name);
    
    printf("\n Peer-Verwaltung (Auszug):\n");
    for (int i = 0; i < shm_ptr->num_peers; i++) {
        printf("  -> Peer Rank %d | PID: %d | Status: %d\n",
               i, 
               shm_ptr->peers[i].pid,
               shm_ptr->peers[i].peer_state);
    }
    printf("====================================================\n\n");

    // 6. Aufräumen
    munmap(shm_ptr, (size_t)shm_stat.st_size);
    close(fd);

    if (OSMP_SUCCESS != OSMP_Finalize()) {
        fprintf(stderr, "Fehler: OSMP_Finalize\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}