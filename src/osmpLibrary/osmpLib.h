/******************************************************************************
 * FILE: osmpLib.h
 * AUTHOR: BS-Team
 * DESCRIPTION:
 * In dieser Datei sind zusätzliche (eigene) Hilfsfunktionen für die **interne**
 * Verwendung der OSMP-Bibliothek definiert.
 ******************************************************************************/

#ifndef __OSMP_LIB_H__
#define __OSMP_LIB_H__

#include <sys/types.h>
#include <semaphore.h>

#include "OSMP.h"

/**
 * @brief Dieses Makro wird verwendet, um den Compiler davon zu überzeugen, dass
 * eine Variable verwendet wird.
 *
 * @param x Die zu verwendende Variable
 */
#define UNUSED(x)  \
    do {           \
        (void)(x); \
    } while (0);

/**
 * @brief  Hier wird ein Fehlercode definiert, der verwendet wird, wenn eine
 * Funktion noch nicht implementiert ist.
 */
#define OSMP_NOT_IMPLEMENTED -1

// !!!!! added more constants here, as writing them into OSMP.h might not be allowed !!!!! //
#define MAX_LOG_ENTRY_LENGTH 128
#define OSMP_MAX_LOG_ENTRIES 20
#define MAX_PEER_MAILBOX_SLOTS 5
#define OSMP_MAX_REQUESTS 256

/**
 * @brief Status eines OSMP-Peer-Prozesses.
 *
 * Dieses Enum beschreibt den Lebenszyklus eines Peer-Prozesses innerhalb
 * einer OSMP-Ausfuehrung.
 *
 * Folgende Faelle sind als Status vorgesehen:
 *
 * - OSMP_PEER_UNUSED:
 *   Statuswert aller Peers zu Beginn (Slot noch nicht verwendet).
 * - OSMP_PEER_STARTING:
 *   Status nach einem erfolgreichen fork() im Runner, Peer befindet sich
 *   in der Start-/Initialisierungsphase.
 * - OSMP_PEER_READY:
 *   Peer hat OSMP_Init() erfolgreich abgeschlossen und ist bereit fuer die
 *   Kommunikation.
 * - OSMP_PEER_FAILED:
 *   Fehler waehrend der Ausfuehrung des Peers, z. B. Init-/Exec-Fehler oder
 *   interner Initialisierungsfehler.
 * - OSMP_PEER_TERMINATED:
 *   Peer wurde geordnet beendet, z. B. durch OSMP_Finalize().
 */
typedef enum {
    OSMP_PEER_UNUSED = 0,   /**< Slot noch nicht verwendet. */
    OSMP_PEER_STARTING,     /**< Peer wurde vom Runner gestartet und befindet sich
                                 in der Start-/Initialisierungsphase. */
    OSMP_PEER_READY,        /**< Peer hat OSMP_Init erfolgreich abgeschlossen und
                                 ist bereit fuer die Kommunikation. */
    OSMP_PEER_FAILED,       /**< Fehler waehrend der Ausfuehrung des Peers
                                 (z. B. Init-/Exec-Fehler). */
    OSMP_PEER_TERMINATED    /**< Peer wurde geordnet beendet und hat seine
                                 OSMP-Ressourcen freigegeben. */
} osmp_peer_state_t;

typedef enum {
    OSMP_STATE_UNINITIALIZED = 0,
    OSMP_STATE_INITIALIZED,
    OSMP_STATE_FINISHED
} osmp_state_t;

// States for Asynchronous worker threads
typedef enum {
    OSMP_REQ_PENDING,
    OSMP_REQ_IN_PROGRESS,
    OSMP_REQ_COMPLETE,
    OSMP_REQ_ERROR
} osmp_req_state_t;

typedef enum {
    OSMP_REQ_SEND,
    OSMP_REQ_RECV,
} osmp_req_type;


/**
 * @brief Peer-spezifische Verwaltungsinformationen.
 *
 * Diese Struktur dient als Vorlage fuer die Verwaltung einzelner OSMP-Peer-
 * Prozesse. Sie enthaelt mindestens die Prozess-ID des Peers sowie einen
 * Statuswert, der den aktuellen Zustand des Peers im OSMP-System beschreibt.
 *
 * Diese Struktur sollte in weiteren Projektverlauf (mit Shared Memory) in das 
 * gemeinsame Speicherlayout uebernommen und um weitere Felder ergaenzt werden
 */
typedef struct {
    pid_t             pid;        /**< Prozess-ID des Peers. */
    int               rank;       /**< Interner Rank des Peers */
    osmp_peer_state_t peer_state; /**< Status des Peers. */
    sem_t             sem_mb_full;
    sem_t             sem_mb_free;
    int               mb_read_idx;
    int               mb_write_idx;
    pthread_mutex_t   mtx_mb;
    pthread_mutex_t   mtx_state;
    int               mailbox[OSMP_MAX_MESSAGES_PROC]; /**< mailbox des peers*/
    /* ...  */
} osmp_peer_t;

typedef struct {
    OSMP_Verbosity  verbosity;
    char            logtext[MAX_LOG_ENTRY_LENGTH];
} osmp_log_t;

typedef struct {
    int             next_index;
    int             len;
    int             sender;
    OSMP_Datatype   datatype;
    char            msg[OSMP_MAX_PAYLOAD_LENGTH];
} osmp_msg_t;

typedef struct
{
    pthread_mutex_t mtx_barrier;
    pthread_cond_t  cond_barrier;
    int             valid; //init flag
    int             threshold;
    int             barrier_count;
    int             cycle;
}osmp_barrier_t;

//unser shared memory schema v.0.4
typedef struct
{
    char            shm_name[OSMP_MAX_SHM_NAME_LENGTH]; // 128chars
    char            log_name[OSMP_MAX_LOGNAME_LENGTH]; // 128chars
    OSMP_Verbosity  verbosity;
    int             num_peers;
    pthread_mutex_t mtx_abort;
    int             abort;
    sem_t           sem_global_free;
    sem_t           sem_init_done;
    sem_t           sem_work_start;
    pthread_mutex_t mtx_global_free;
    int             msg_slots_head;
    osmp_msg_t      message_slots[OSMP_MAX_SLOTS];
    osmp_barrier_t  barrier;
    osmp_barrier_t  gather_barrier;
    osmp_msg_t      gather_space[OSMP_MAX_SLOTS];
    osmp_peer_t     peers[];
}shm_layout_t;






#endif  // __OSMP_LIB_H__
