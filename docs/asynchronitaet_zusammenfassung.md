# Asynchronität in OSMP: Architektur und Kompatibilität

## 1. Asynchrone Aufrufe (`OSMP_ISend`, `OSMP_IRecv`)
Bei asynchronen Aufrufen wird der Kopiervorgang in einen separaten Thread ausgelagert. Die Funktion initialisiert eine Request-Struktur und erzeugt mittels `pthread_create` einen Hintergrund-Thread. Der Main-Thread des Aufrufers wird nicht blockiert und kehrt unmittelbar nach der Thread-Erstellung zurück.

```c
// Auszug OSMP_ISend (osmpLib.c)
int OSMP_ISend(const void *buf, int count, OSMP_Datatype datatype, int dest, OSMP_Request request) {
    // Initialisierung der Request-Parameter
    osmp_request_internal_t *req = (osmp_request_internal_t *)request;
    req->type = OSMP_REQ_SEND;
    /* ... */
    
    // Erzeugung des Background-Threads
    if (Create_Request_Thread(request) != 0) {
        return -1; // Fehlerbehandlung
    }
    
    // Unmittelbare Rückkehr ohne Blockierung (Non-blocking)
    return OSMP_SUCCESS;
}
```

## 2. Synchrone Aufrufe (`OSMP_Send`, `OSMP_Recv`)
Synchrone Aufrufe fungieren architektonisch als Wrapper für die asynchronen Implementierungen. Sie initiieren denselben Hintergrund-Thread, erzwingen jedoch im Anschluss eine Blockierung des Main-Threads durch den Aufruf von `OSMP_Wait`. Dies geschieht intern über die Funktion `pthread_join`, welche erst zurückkehrt, wenn der Hintergrund-Thread seine Ausführung beendet hat.

```c
// Auszug OSMP_Send (osmpLib.c)
int OSMP_Send(const void *buf, int count, OSMP_Datatype datatype, int dest) {
    OSMP_Request req;
    OSMP_CreateRequest(&req);
    
    // Aufruf der asynchronen Funktion
    OSMP_ISend(buf, count, datatype, dest, req); 
    
    // Blockierung des Main-Threads bis Abschluss der Übertragung
    int rv = OSMP_Wait(req); 
    
    OSMP_RemoveRequest(&req);
    return rv;
}
```

## 3. Kompatibilität der Schnittstellen
Die Interoperabilität zwischen synchronen und asynchronen Aufrufen (z.B. `OSMP_Send` und `OSMP_IRecv`) ist uneingeschränkt sichergestellt. Unabhängig vom aufrufenden Kontext wird die Speicheroperation (`memcpy`) und die Semaphoren-Verwaltung (`sem_wait`, `sem_post`) stets von denselben Worker-Routinen (`thread_send`, `thread_recv`) ausgeführt. Diese greifen einheitlich auf die in der Shared-Memory-Struktur (`shm_layout_t`) definierten Message-Slots und Mailboxen zu.

```c
// Einheitlicher Zugriff aus Worker-Thread (thread_send in osmpLib.c)

// 1. Reservierung eines Slots im Shared Memory
sem_wait(&shared->sem_global_free);
pthread_mutex_lock(&shared->mtx_global_free);
/* ... Indexbestimmung ... */

// 2. Speicherkopie in den reservierten Slot
memcpy(shared->message_slots[reserved_index].msg, buf, total_len);
/* ... */

// 3. Zustellung der Referenz an die Peer-Mailbox
pthread_mutex_lock(&shared->peers[peer_index].mtx_mb);
shared->peers[peer_index].mailbox[current_write_idx] = reserved_index;
pthread_mutex_unlock(&shared->peers[peer_index].mtx_mb);

// 4. Signalisierung des Zustands an den Empfänger
sem_post(&shared->peers[peer_index].sem_mb_full);
```

## 4. Thread- und prozessunabhängige Synchronisation
Die verwendeten Synchronisationsprimitive (Semaphoren) sind im Shared Memory allokiert und agieren thread- sowie prozessunabhängig. Der Linux-Kernel verwaltet den Zustand des Semaphors rein basierend auf der allokierten Speicheradresse, ohne Rücksicht auf die Identität der zugreifenden Threads.

Ein Worker-Thread (z.B. `thread_send`) inkrementiert den Zähler mittels `sem_post`, woraufhin der Kernel denjenigen blockierten Thread (z.B. `thread_recv`) deblockiert, der an derselben Speicheradresse mittels `sem_wait` wartet. Eine direkte Kommunikation zwischen den beteiligten Threads findet architektonisch nicht statt.
