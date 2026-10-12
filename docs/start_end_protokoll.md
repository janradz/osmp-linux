Start und Endprotokoll

1. Runner vor fork() setzt peers auf OSMP_PEER_STARTING.

2.Runner forkt Peers nacheinander

3. sobald Peers gestartet -> Runner wartet N mal auf Semaphore sem_init_done

4a. (Erfolg) wenn alle Peers unfallfrei bereit sind: Runner gibt alle peers frei mit sem_work_start

4b (Fehler) Runner setzt globable abort flag, weckt alle wartenden peers auf, damit sie sich beenden können. räumt danach auf

In OSMP_Init():

1. Direkter check ob ein anderer peer abgestürzt ist -> globable abort flag

2a. (Fehlschlag) wenn init fehlschlägt -> OSMP_PEER_FAILED status.

2b. (Erfolg) wenn init erfolgt -> OSMP_PEER_READY

3a. (Fehlschlag) nach wakeup vom blockieren check: gab beim warten einen peer fehlstart? wenn ja, abortflag -> OSMP_PEER_FAILED

3b. (Erfolg) OSMP_SUCCESS return

Endprotokoll:

in OSMP_Finalize():

1. check ob peer überhaupt initialsiert war
   wenn nicht -> return -1

2. OSMP_PEER_TERMINATED status setzen

3. munmap -> OSMP_STATE_FINISHED setzen

im Runner:

1. Runner geht durch Peer-Array im SHM und checked ob alle gestarteten Peers bei TERMINATED oder FAILED erreicht haben

2. shm_unlink cleanup

