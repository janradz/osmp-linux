/******************************************************************************
 * FILE: osmpLib.c
 * AUTHOR: BS-Team
 * DESCRIPTION:
 * In dieser Datei sind Funktionen der OSMP Bibliothek implementiert
 ******************************************************************************/

#include "osmpLib.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

// Internal Struct for opaque request
typedef struct {
    pthread_t worker;
    osmp_req_type type;
    int state;
    pthread_mutex_t mtx_state;
    int* sender;
    int receiver;

    int send_data_count;
    int recv_data_count;
    int *real_recv_data_count;
    OSMP_Datatype datatype;
    const void* send_buffer;
    void* recv_buffer;
    int joined; // Merkt sich, ob pthread_join gerufen wurde
} osmp_request_internal_t;


static void set_req_state(osmp_request_internal_t* req, int state) {
    pthread_mutex_lock(&req->mtx_state);
    req->state = state;
    pthread_mutex_unlock(&req->mtx_state);
}

// --- Globale Variablen ---

static osmp_state_t osmp_state = OSMP_STATE_UNINITIALIZED;
static int osmp_rank = -1;
static int osmp_peer_index = -1;
shm_layout_t* shared = NULL;
FILE* f = NULL;
static pid_t osmp_main_tid = -1; //für thread id des main threads
osmp_request_internal_t* requests[OSMP_MAX_REQUESTS] = { NULL };



void* thread_send(void* request) {
    osmp_request_internal_t* req = request;
    set_req_state(req, OSMP_REQ_IN_PROGRESS);

    const void *buf = req->send_buffer;
    int count = req->send_data_count;
    OSMP_Datatype datatype = req->datatype;
    int dest = req->receiver;

    if (count > OSMP_MAX_PAYLOAD_LENGTH) {
        OSMP_Log(OSMP_LOG_BIB, "[OSMP_Send] Error: payload too large\n");
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    int peer_index = -1;
    for (int i = 0; i < shared->num_peers; i++) {
        if (shared->peers[i].rank == dest) {
            peer_index = i;
            break;
        }
    }
    if (peer_index == -1) {
        OSMP_Log(OSMP_LOG_BIB,"[OSMP_Send] Error: destination peer does not exist\n");
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    if (sem_wait(&shared->peers[peer_index].sem_mb_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    if (sem_wait(&shared->sem_global_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    if (pthread_mutex_lock(&shared->mtx_global_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    int reserved_index = shared->msg_slots_head;
    shared->msg_slots_head = shared->message_slots[reserved_index].next_index;

    if (pthread_mutex_unlock(&shared->mtx_global_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    uint total_len = 0;
    OSMP_SizeOf(datatype, &total_len);
    total_len = total_len * (uint) count;
    if (total_len > OSMP_MAX_PAYLOAD_LENGTH) {
        OSMP_Log(OSMP_LOG_BIB,"[OSMP_Send] Error: payload too large\n");
        if (pthread_mutex_lock(&shared->mtx_global_free) != 0) {
            set_req_state(req, OSMP_REQ_ERROR);
            return (void*)1;
        }
        shared->message_slots[reserved_index].next_index = shared->msg_slots_head;
        shared->msg_slots_head = reserved_index;

        if (pthread_mutex_unlock(&shared->mtx_global_free) != 0) {
            set_req_state(req, OSMP_REQ_ERROR);
            return (void*)1;
        }
        if (sem_post(&shared->sem_global_free) != 0) {
            set_req_state(req, OSMP_REQ_ERROR);
            return (void*)1;
        }
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    shared->message_slots[reserved_index].len = (int)total_len;
    shared->message_slots[reserved_index].datatype = datatype;
    shared->message_slots[reserved_index].sender = osmp_rank;

    memcpy(shared->message_slots[reserved_index].msg, buf, total_len);

    if (pthread_mutex_lock(&shared->peers[peer_index].mtx_mb) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    int current_write_idx = shared->peers[peer_index].mb_write_idx;
    shared->peers[peer_index].mailbox[current_write_idx] = reserved_index;
    shared->peers[peer_index].mb_write_idx = (current_write_idx + 1) % OSMP_MAX_MESSAGES_PROC;

    if (pthread_mutex_unlock(&shared->peers[peer_index].mtx_mb) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    if (sem_post(&shared->peers[peer_index].sem_mb_full) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    set_req_state(req, OSMP_REQ_COMPLETE);
    return 0;
}

void* thread_recv(void* request) {
    osmp_request_internal_t* req = request;
    set_req_state(req, OSMP_REQ_IN_PROGRESS);

    void *buf = req->recv_buffer;
    int count = req->recv_data_count;
    OSMP_Datatype datatype = req->datatype;
    int *source = req->sender;
    int *len = req->real_recv_data_count;

    // unused macros removed since count and datatype are used

    if (sem_wait(&shared->peers[osmp_rank].sem_mb_full) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    if (pthread_mutex_lock(&shared->peers[osmp_rank].mtx_mb) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    int reserved_index = shared->peers[osmp_rank].mailbox[shared->peers[osmp_rank].mb_read_idx];
    int current_read_idx = shared->peers[osmp_rank].mb_read_idx;

    shared->peers[osmp_rank].mb_read_idx = (current_read_idx + 1) % OSMP_MAX_MESSAGES_PROC;

    if (pthread_mutex_unlock(&shared->peers[osmp_rank].mtx_mb) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    if (sem_post(&shared->peers[osmp_rank].sem_mb_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    if (reserved_index == -1) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    *len = shared->message_slots[reserved_index].len;
    *source = shared->message_slots[reserved_index].sender;
    uint wanted_len = 0;
    OSMP_SizeOf(datatype, &wanted_len);
    wanted_len = wanted_len * (uint)count;
    if (wanted_len > (uint)*len) {
        OSMP_Log(OSMP_LOG_ALL,"[OSMP_Recv] Requested datacount larger than available data, truncating\n");
        memcpy(buf, shared->message_slots[reserved_index].msg, (uint)shared->message_slots[reserved_index].len);
    }
    else if (wanted_len < (uint)*len) {
        *len = (int)wanted_len;
        OSMP_Log(OSMP_LOG_ALL,"[OSMP_Recv] Requested datacount smaller than available data\n");
        memcpy(buf, shared->message_slots[reserved_index].msg, wanted_len);
    }
    else {
        *len = (int)wanted_len;
        memcpy(buf, shared->message_slots[reserved_index].msg, wanted_len);
    }

    shared->message_slots[reserved_index].next_index = -1;

    if (pthread_mutex_lock(&shared->mtx_global_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    shared->message_slots[reserved_index].next_index = shared->msg_slots_head;
    shared->msg_slots_head = reserved_index;

    if (pthread_mutex_unlock(&shared->mtx_global_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }
    if (sem_post(&shared->sem_global_free) != 0) {
        set_req_state(req, OSMP_REQ_ERROR);
        return (void*)1;
    }

    set_req_state(req, OSMP_REQ_COMPLETE);
    return 0;
}

int Create_Request_Thread(OSMP_Request* request) {
    int rv;

    osmp_request_internal_t* req = (osmp_request_internal_t*)request;
    if (req->type == OSMP_REQ_SEND) {
        rv = pthread_create(&req->worker, NULL, thread_send, req);
    }
    else {
        rv = pthread_create(&req->worker, NULL, thread_recv, req);
    }
    if (rv != 0) {
        return -1;
    }
    return 0;
}

static int OSMP_ThreadCheck(void)
{
    if (osmp_main_tid == -1) {
        return -1;
    }


    //thread absicherung
    if (syscall(SYS_gettid) != osmp_main_tid){
        OSMP_Log(OSMP_LOG_ALL,"[BARRIER] ERROR: Called by non main thread.");
        return -1;
    }

    return 0;
}

static int OSMP_UpdateState(osmp_peer_state_t state)
{
    if (shared == NULL) return -1;
    if (osmp_rank < 0 || osmp_rank >= shared->num_peers) return -1;

    if (pthread_mutex_lock(&shared->peers[osmp_rank].mtx_state) != 0)
    {
        perror("pthread_mutex_lock failed");
        return -1;
    }

    if (shared->peers[osmp_rank].peer_state == OSMP_PEER_FAILED)
    {
        OSMP_Log(OSMP_LOG_BIB, "[UPDATESTATE] Peer has already failed.\n");
    }
    shared->peers[osmp_rank].peer_state = state;

    if (pthread_mutex_unlock(&shared->peers[osmp_rank].mtx_state) != 0)
    {
        perror("pthread_mutex_lock failed");
        return -1;
    }
    return OSMP_SUCCESS;
}
int OSMP_GetSuccess(void) {
    return OSMP_SUCCESS;
}

int OSMP_GetMaxMessagesProc(void) {
    return OSMP_MAX_MESSAGES_PROC;
}

int OSMP_GetMaxSlots(void) {
    return OSMP_MAX_SLOTS;
}

int OSMP_GetMaxPayloadLength(void) {
    return OSMP_MAX_PAYLOAD_LENGTH;
}

int OSMP_GetMaxShmNameLength(void) {
    return OSMP_MAX_SHM_NAME_LENGTH;
}

int OSMP_GetMaxLogNameLength(void) {
    return OSMP_MAX_LOGNAME_LENGTH;
}

int OSMP_GetShmName(char* name) {
    if (name == NULL) return -1;

    pid_t parent_pid = getppid();
    int ret = snprintf(name, OSMP_MAX_SHM_NAME_LENGTH,"/osmp_shm_%d", parent_pid);

    if (ret < 0) {
        return -1;
    }

    if (ret >= OSMP_MAX_SHM_NAME_LENGTH) {
        fprintf(stdout, "output was truncated\n");
        return -1;
    }
    return OSMP_SUCCESS;
}

int OSMP_Init(const int *argc, char ***argv) {
    UNUSED(argc);
    UNUSED(argv);

    if (osmp_state != OSMP_STATE_UNINITIALIZED) {
        fprintf(stderr, "[OSMP_Init] Error: OSMP is already initialized or finished.\n");
        return -1;
    }

    char shm_name[OSMP_MAX_SHM_NAME_LENGTH];
    if (OSMP_GetShmName(shm_name) != OSMP_SUCCESS) {
        return -1;
    }

    const int shm = shm_open(shm_name, O_RDWR, 0);
    if (shm == -1) {
        fprintf(stderr, "[OSMP_Init] Error opening shared memory segment\n");
        return -1;
    }

    struct stat fd_meta;
    if (fstat(shm, &fd_meta) == -1) {
        fprintf(stderr, "[OSMP_Init] Error: fstat failed\n");
        close(shm);
        return -1;
    }

    void* map = mmap(NULL, (size_t)fd_meta.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    if (map == MAP_FAILED) {
        perror("[OSMP_Init] Error: mmap failed");
        close(shm);
        return -1;
    }

    shared = (shm_layout_t*) map;
    if (shared->abort == 1) {
        fprintf(stderr, "[OSMP_Init] Fatal: Breaking off peer creation because another peer failed to initialize\n");
        size_t unmap_size = sizeof(shm_layout_t) + ((uint)shared->num_peers * sizeof(osmp_peer_t));
        if (munmap(shared, unmap_size) == -1) {
            perror("[OSMP_Init] Error: munmap failed");
            return -1;
        }
        close(shm);
        return -1;
    }

    osmp_state = OSMP_STATE_INITIALIZED;
    OSMP_Log(OSMP_LOG_BIB, "[INIT] Initializing OSMP system for peer...\n");

    pid_t pid = getpid();
    osmp_main_tid = (pid_t)syscall(SYS_gettid);
    int peer_found = 0;
    for (int retry = 0; retry < 10000 && !peer_found; retry++) {
        for (int i = 0; i < shared->num_peers; i++) {
            if (shared->peers[i].pid == pid) {
                osmp_rank = shared->peers[i].rank;
                osmp_peer_index = i;
                peer_found = 1;
                OSMP_UpdateState(OSMP_PEER_READY);
                break;
            }
        }
        if (!peer_found) {
            usleep(1000); // 1 ms warten und erneut versuchen
        }
    }

    if (!peer_found) {
        fprintf(stderr, "[OSMP_Init] Fatal: Could not find PID %d in shared memory after 10 seconds!\n", pid);
        return -1;
    }

    if (peer_found == 0) {
        OSMP_Log(OSMP_LOG_BIB, "[INIT] Peer could not be initialized, peer not found\n");
        shared->abort = 1;
        munmap(shared, (size_t)fd_meta.st_size);
        shared = NULL;
        osmp_state = OSMP_STATE_UNINITIALIZED;
        close(shm);
        return -1;
    }

    close(shm);


    char msg[MAX_LOG_ENTRY_LENGTH];
    snprintf(msg, sizeof(msg), "[INIT] Process %d successfully initialized with Rank %d\n", pid, osmp_rank);
    OSMP_Log(OSMP_LOG_BIB, msg);
    //zählt erfolreiche initialisierung
    if (sem_post(&shared->sem_init_done) != 0)
    {
        if (osmp_peer_index >= 0)
        {
            OSMP_UpdateState(OSMP_PEER_FAILED);
            shared->abort = 1;
        }
        perror("[OSMP_Init] sem_post(sem_init_done) failed");
        return -1;
    }
    if (sem_wait(&shared->sem_work_start) != 0) {
        perror("sem_wait(sem_start_work) failed");
        return -1;
    }
    return OSMP_SUCCESS;
}

int OSMP_SizeOf(OSMP_Datatype datatype, unsigned int *size) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (size == NULL) return -1;

    switch (datatype) {
    case OSMP_SHORT:
        *size = sizeof(short);
        break;
    case OSMP_INT:
        *size = sizeof(int);
        break;
    case OSMP_LONG:
        *size = sizeof(long);
        break;
    case OSMP_UNSIGNED_CHAR:
        *size = sizeof(unsigned char);
        break;
    case OSMP_UNSIGNED:
        *size = sizeof(unsigned int);
        break;
    case OSMP_UNSIGNED_LONG:
        *size = sizeof(unsigned long);
        break;
    case OSMP_FLOAT:
        *size = sizeof(float);
        break;
    case OSMP_DOUBLE:
        *size = sizeof(double);
        break;
    case OSMP_BYTE:
        *size = 1;
        break;
    default:
        return -1;
    }
    return OSMP_SUCCESS;
}

int OSMP_Size(int *size) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (size == NULL) return -1;

    *size = shared->num_peers;
    return OSMP_SUCCESS;
}

int OSMP_Rank(int *rank) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (rank == NULL) return -1;

    *rank = osmp_rank;
    return OSMP_SUCCESS;
}

int OSMP_Send(const void *buf, int count, OSMP_Datatype datatype, int dest) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (buf == NULL) return -1;
    if (OSMP_ThreadCheck() != 0) return -1;

    OSMP_Request req;
    if (OSMP_CreateRequest(&req) != OSMP_SUCCESS) return -1;
    if (OSMP_ISend(buf, count, datatype, dest, req) != OSMP_SUCCESS) {
        OSMP_RemoveRequest(&req);
        return -1;
    }
    int rv = OSMP_Wait(req);
    OSMP_RemoveRequest(&req);
    return rv;
}

int OSMP_Recv(void *buf, int count, OSMP_Datatype datatype, int *source, int *len) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (buf == NULL || source == NULL || len == NULL) return -1;
    if (OSMP_ThreadCheck() != 0) return -1;

    OSMP_Request req;
    if (OSMP_CreateRequest(&req) != OSMP_SUCCESS) return -1;
    if (OSMP_IRecv(buf, count, datatype, source, len, req) != OSMP_SUCCESS) {
        OSMP_RemoveRequest(&req);
        return -1;
    }
    int rv = OSMP_Wait(req);
    OSMP_RemoveRequest(&req);
    return rv;
}

int OSMP_Finalize(void) {
    if (osmp_state != OSMP_STATE_INITIALIZED) {
        fprintf(stderr, "[OSMP_Finalize] Error: OSMP is not initialized or already finished.\n");
        return -1;
    }

    if (osmp_peer_index >= 0) {
        OSMP_UpdateState(OSMP_PEER_FAILED);
        char msg[MAX_LOG_ENTRY_LENGTH];
        snprintf(msg, sizeof(msg), "[FINALIZE] Peer with Rank %d terminated\n", osmp_rank);
        OSMP_Log(OSMP_LOG_BIB, msg);
    } else {
        return -1;
    }
    if (OSMP_ThreadCheck() != 0) return -1;

    for (int i = 0; i < OSMP_MAX_REQUESTS; i++) {
        if (requests[i] != NULL) {
            pthread_mutex_lock(&requests[i]->mtx_state);
            int is_joined = requests[i]->joined;
            osmp_req_type type = requests[i]->type;
            pthread_mutex_unlock(&requests[i]->mtx_state);

            if (!is_joined) {
                OSMP_Log(OSMP_LOG_ALL, "Cleaning up unresolved Request");
                // Nur RECV-Threads abbrechen, da sie sonst bei fehlenden Nachrichten für immer hängen.
                // SEND-Threads lassen wir in Ruhe fertig laufen (implizites Warten).
                if (type == OSMP_REQ_RECV) {
                    pthread_cancel(requests[i]->worker);
                }
                pthread_join(requests[i]->worker, NULL);
            }
            pthread_mutex_destroy(&requests[i]->mtx_state);
            free(requests[i]);
            requests[i] = NULL;
        }
    }

    size_t unmap_size = sizeof(shm_layout_t) + ((uint)shared->num_peers * sizeof(osmp_peer_t));
    if (munmap(shared, unmap_size) == -1) {
        perror("[OSMP_Finalize] Error: munmap failed");
        return -1;
    }

    shared = NULL;
    if (f != NULL)
    {
        fclose(f);
    }
    osmp_state = OSMP_STATE_FINISHED;
    return OSMP_SUCCESS;
}
static int osmp_internal_barrier(osmp_barrier_t *barrier) {
     if (osmp_state != OSMP_STATE_INITIALIZED || shared == NULL) {
        fprintf(stderr, "[OSMP_Barrier] Error: OSMP not initialized.\n");
        return -1;
    }

    if (barrier->valid != 1)
    {
        OSMP_Log(OSMP_LOG_ALL, "[OSMP_Barrier] Error: Barrier not properly initialized by runner.\n");
        return -1;
    }
    if (OSMP_ThreadCheck() != 0) return -1;

    int cancel_state;
    int tmp_state;

    //kritischer bereich:
    if (pthread_mutex_lock(&barrier->mtx_barrier) != 0) {
        perror("[OSMP_Barrier] pthread_mutex_lock failed");
        return -1;
    }
    //welcher cycle gerade?
    int my_cycle = barrier->cycle;

    //komme an barrier -> fehlende prozesse dekrementieren
    barrier->barrier_count--;

    //check: bin ich letzer peer?
    //wenn ja: barrier voll, alle dürfen weitr
    //ergo cycle erhöhen, alter cycle invalid
    if (barrier->barrier_count == 0)
    {
        barrier->cycle++;
        //reset count für nächsten barrier aufruf
        barrier->barrier_count = barrier->threshold;
        //alle threads wecken
        pthread_cond_broadcast(&barrier->cond_barrier);

    }
    else //wenn nicht der letzte peer:
    {
        //CHECKILISTE: Verhindern, dass der Thread genau beim
        // Warten gekillt wird und das Mutex für immer gesperrt bleibt.
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state);

        //CHECKLISTE: spurious wakeup prevention: so lange warten bis letzer peer cycle erhöht
        while (my_cycle == barrier->cycle) {
            pthread_cond_wait(&barrier->cond_barrier, &barrier->mtx_barrier);
        }

        //wakeup + cycle hoch -> cancelstate wiederherstellen
        pthread_setcancelstate(cancel_state, &tmp_state);

    }

    //kritschen bereich verlassen
    if (pthread_mutex_unlock(&barrier->mtx_barrier) != 0) {
        perror("[OSMP_Barrier] pthread_mutex_unlock failed");
        return -1;
    }

    return OSMP_SUCCESS;
}

int OSMP_Barrier(void) {
    return osmp_internal_barrier(&shared->barrier);
}

int OSMP_Gather(void *sendbuf, int sendcount, OSMP_Datatype sendtype,
                void *recvbuf, int recvcount, OSMP_Datatype recvtype, int root) {
    UNUSED(recvcount);
    if (osmp_state != OSMP_STATE_INITIALIZED) {
        OSMP_Log(OSMP_LOG_ALL, "[OSMP Gather]: Peer uninitialized");
        return -1;
    }
    if (osmp_rank == root && (sendbuf == NULL || recvbuf == NULL)) {
        OSMP_Log(OSMP_LOG_ALL, "[OSMP Gather]: Root parameter failure");
        return -1;
    }
    if (osmp_rank != root && sendbuf == NULL){
        OSMP_Log(OSMP_LOG_ALL, "[OSMP Gather]: Peer has no send buffer");
        return -1;
    }
    if (OSMP_ThreadCheck() != 0) return -1;

    uint datatype_size;
    if (OSMP_SUCCESS != OSMP_SizeOf(recvtype, &datatype_size)) {
        OSMP_Log(OSMP_LOG_BIB, "[OSMP Gather]: Failed to resolve datatype size");
        return -1;
    }
    int total_len = 0;
    total_len = (int)datatype_size * sendcount;

    if (osmp_rank == root && sendcount > recvcount) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[OSMP Gather]: recv buffer too small");
        OSMP_Log(OSMP_LOG_BIB, msg);
        return -1;
    }

    if (total_len > OSMP_MAX_PAYLOAD_LENGTH) {
        OSMP_Log(OSMP_LOG_BIB,"[OSMP_Send] Error: payload too large");
        return -1;
    }

    // Wait for all peers, if we don't, we cannot gather messages from each one
    if (OSMP_SUCCESS != osmp_internal_barrier(&shared->gather_barrier)) {
        OSMP_Log(OSMP_LOG_BIB, "[OSMP Gather]: Barrier Failed");
        return -2;
    }

    shared->gather_space[osmp_rank].len = total_len;
    shared->gather_space[osmp_rank].datatype = sendtype;
    shared->gather_space[osmp_rank].sender = osmp_rank;

    memcpy(shared->gather_space[osmp_rank].msg, sendbuf, (uint)total_len);

    // Wait for everyone to write their data into the gather_space
    if (OSMP_SUCCESS != osmp_internal_barrier(&shared->gather_barrier)) {
        OSMP_Log(OSMP_LOG_BIB, "[OSMP Gather]: Barrier Failed");
        return -2;
    }

    if (osmp_rank == root) {
        uint expected_len = (uint)((uint)recvcount * datatype_size);
        for (int i = 0; i < shared->num_peers; i++) {
            uint copy_len = (uint)shared->gather_space[i].len;
            if (copy_len > expected_len) {
                copy_len = expected_len;
            }
            memcpy((char *)recvbuf + (uint)(i * recvcount) * datatype_size, shared->gather_space[i].msg, copy_len);
        }
    }


    return OSMP_SUCCESS;
}

int OSMP_ISend(const void *buf, int count, OSMP_Datatype datatype, int dest, OSMP_Request request) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (buf == NULL) return -1;
    if (OSMP_ThreadCheck() != 0) return -1;

    osmp_request_internal_t *req = (osmp_request_internal_t *)request;
    req->type = OSMP_REQ_SEND;
    req->receiver = dest;
    req->send_buffer = buf;
    req->send_data_count = count;
    req->datatype = datatype;
    set_req_state(req, OSMP_REQ_PENDING);


    if (Create_Request_Thread(request) != 0) {
        OSMP_Log(OSMP_LOG_ALL, "[OSMP ISend]: Failed to start thread");
        return -1;
    }

    return OSMP_SUCCESS;
}

int OSMP_IRecv(void *buf, int count, OSMP_Datatype datatype, int *source, int *len, OSMP_Request request) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (buf == NULL || source == NULL || len == NULL) return -1;
    if (OSMP_ThreadCheck() != 0) return -1;

    osmp_request_internal_t* req = (osmp_request_internal_t *)request;
    req->type = OSMP_REQ_RECV;
    req->sender = source;
    req->recv_buffer = buf;
    req->recv_data_count = count;
    req->real_recv_data_count = len;
    req->datatype = datatype;
    set_req_state(req, OSMP_REQ_PENDING);


    if (Create_Request_Thread(request) != 0) {
        OSMP_Log(OSMP_LOG_ALL, "[OSMP IRecv]: Failed to start thread");
        return -1;
    }
    return OSMP_SUCCESS;
}


int OSMP_Test(OSMP_Request request, int *flag) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (flag == NULL || request == NULL) return -1;
    osmp_request_internal_t *req = request;
    pthread_mutex_lock(&req->mtx_state);
    *flag = req->state;
    pthread_mutex_unlock(&req->mtx_state);

    return OSMP_SUCCESS;
}

int OSMP_Wait(OSMP_Request request) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;

    osmp_request_internal_t *req = request;

    void* rv = NULL;
    
    pthread_mutex_lock(&req->mtx_state);
    int already_joined = req->joined;
    pthread_mutex_unlock(&req->mtx_state);

    if (!already_joined) {
        pthread_join(req->worker, &rv);
        
        pthread_mutex_lock(&req->mtx_state);
        req->joined = 1;
        pthread_mutex_unlock(&req->mtx_state);

        if ((long)rv != 0) {
            OSMP_Log(OSMP_LOG_BIB, "[OSMP Wait]: Thread exited unexpectedly\n");
            return -1;
        }
    }

    return OSMP_SUCCESS;

}

int OSMP_CreateRequest(OSMP_Request *request) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (request == NULL) return -1;
    osmp_request_internal_t *req = malloc(sizeof(osmp_request_internal_t));
    if (req == NULL) return -1;
    pthread_mutex_init(&req->mtx_state, NULL);
    req->joined = 0;

    for (int i = 0; i < OSMP_MAX_REQUESTS; i++) {
        if (requests[i] == NULL) {
            requests[i] = req;
            OSMP_Log(OSMP_LOG_ALL, "[OSMP ISend]: Added new Request");
            break;
        }
    }

    *request = req;

    return OSMP_SUCCESS;
}

int OSMP_RemoveRequest(OSMP_Request *request) {
    if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
    if (request == NULL || *request == NULL) return -1;

    osmp_request_internal_t *req = (osmp_request_internal_t *)*request;
    
    for (int i = 0; i < OSMP_MAX_REQUESTS; i++) {
        if (requests[i] == req) {
            requests[i] = NULL;
            break;
        }
    }

    pthread_mutex_destroy(&req->mtx_state);
    free(req);
    *request = NULL;

    return OSMP_SUCCESS;
}

int OSMP_Log(OSMP_Verbosity verbosity, char *message) {
    if (shared == NULL || osmp_state != OSMP_STATE_INITIALIZED) {
        fprintf(stdout, "Failed to log the following message: %s", message);
        return -1;
    }
    if (message == NULL) return -1;

    //check: sollen wir überhaupt loggen?
    if (verbosity > shared->verbosity || shared->log_name[0] == '\0') {
        return OSMP_SUCCESS; // ignorieren den log, Loglevel zu niedrig oder kein File
    }
    if (f == NULL)
    {
        f = fopen(shared->log_name, "a");
        if (f == NULL)
        {
            fprintf(stderr, "OSMP_Log error: Could not open %s\n", shared->log_name);
            return -1;
        }
    }



    //buffer darf nicht vollaufen
    char buffer[MAX_LOG_ENTRY_LENGTH + 128];

    //timestamp generieren wie in runner

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME,&ts);
    struct tm* t = localtime(&ts.tv_sec);
    double millis = (double)ts.tv_nsec / 1.0e6;
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%FT%T", t);


    //in buffer schreiben
    snprintf(buffer, sizeof(buffer), "%s%f %d %d %s\n", time_str,millis, verbosity, getpid(), message);

    //append modus hilft in POSIX bei race condition lustigerweise, vielleicht durch mutexe austauschen später



    fputs(buffer, f); // fputs schreibt string in datei
    return OSMP_SUCCESS;
}