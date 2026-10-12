/******************************************************************************
 * FILE: echoall.c
 * AUTHOR: BS-Team
 * DESCRIPTION:
 * First OSMP Executable, it simply echoes all arguments passed to it.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "../osmpLibrary/OSMP.h"
#include "../osmpLibrary/osmpLib.h"

int main(const int argc, char** argv)
{
    if (OSMP_Init(&argc, &argv) != OSMP_SUCCESS) {
        fprintf(stderr, "Error in OSMP_Init\n");
    }


    const pid_t pid = getpid();
    const pid_t ppid = getppid();
    const pid_t tid = (pid_t)syscall(SYS_gettid);
    int rank = 0;
    OSMP_Rank(&rank);


    if (argc < 2)
    {
        fprintf(stderr, "Error in echoALl: No Arguments passed \n");
        return EXIT_FAILURE;
    }
    //Ausgabe aller übergebenen Kommandozeilenargumente auf stdout (argv[1]–argv[n]).
    //Numerische Durchnummerierung der Argumente in der Reihenfolge ihres Auftretens.
    for (int i = 1; i < argc; i++)
    {
        fprintf(stdout, "Argument %d: %s\n", i, argv[i]);
    }

    //Mehrfache Ausgabe der eigenen Prozesskennzahlen
    for (int j = 0; j < 3; j++)
    {
        fprintf(stdout, "[PID: %d | PPID: %d | TID: %d | Rank %d]\n", pid, ppid, tid, rank);
        sleep(1);
    }

    OSMP_Finalize();
    return 0;
}
