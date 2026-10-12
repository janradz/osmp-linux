/******************************************************************************
 * FILE: osmpRun.c
 * AUTHOR: Darius Malysiak
 * DESCRIPTION:
 * Diese Datei enthält die main Funktion für den OSMP-Runner.
 ******************************************************************************/

#include "osmpRun.h"
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

//runner context
typedef struct
{
    int processcount;
    int log_level;
    const char* log_file;
    char** exec_argv; // Das Executable und seine Argumente
    char shm_name[OSMP_MAX_SHM_NAME_LENGTH]; // Der generierte Name
    size_t shm_size; // Die berechnete Größe
    shm_layout_t* layout; // Pointer auf das gemappte SHM
} runner_ctx_t;


shm_layout_t* setup_shm(const char* shm_name, const int processcount, size_t* out_size)
{
    if (shm_name == NULL || out_size == NULL || processcount <= 0)
    {
        errno = EINVAL;
        return NULL;
    }

    // //grosse dynamisch berechnen für anzahl der processes die übergeben wurden
    const size_t p_count = (size_t)processcount;
    const size_t shm_base_size = sizeof(shm_layout_t);
    const size_t peer_size = sizeof(osmp_peer_t);
    //
    // //super elaborate overflow checks für unser shm_size
    // if (n > SIZE_MAX / elem)
    // {
    //     errno = EOVERFLOW;
    //
    //     return NULL;
    // }
    // const size_t peer_bytes = n * elem;
    //
    // if (base > SIZE_MAX - peer_bytes)
    // {
    //     errno = EOVERFLOW;
    //     return NULL;
    // }
    // const size_t shm_size = base + peer_bytes;

    const size_t shm_size = shm_base_size + (p_count * peer_size);
    fprintf(stdout, "size of shared memory struct is: %lu \n", shm_size);

    *out_size = shm_size; //fürs unmap


    //erstelle shm
    const int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0660);
    if (fd == -1)
    {
        perror("Error in Runner: shm_open failed");
        return NULL;
    }
    //großer errorchecking block

    if ((size_t)(off_t)shm_size != shm_size)
    {
        errno = EOVERFLOW;
        fprintf(stderr, "Error in Runner: shm_size overflow\n");
        return NULL;
    }


    //groesse festlegen
    if (ftruncate(fd, (off_t)shm_size) == -1)
    {
        perror("Error in Runner: ftruncate failed");
        close(fd);
        shm_unlink(shm_name); //bei fehler direkt aufräumen
        return NULL;
    }
    //mapping teil
    void* shm_ptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); //offset erstmal 0
    if (shm_ptr == MAP_FAILED)
    {
        perror("Error in Runner: mmap failed");
        close(fd);
        shm_unlink(shm_name); //selbes prinzip, aufräumen
        return NULL;
    }
    fprintf(stdout, "shm creation pointer %p\n", shm_ptr);
    fprintf(stdout, "creation size %lu\n", shm_size);
    //super wichtig fd schliessen
    close(fd);
    return (shm_layout_t*)shm_ptr;
    //wenn alles lief können wir jetzt über shm_ptr ins shm schreiben mit dem struct als layout
}

int init_shared_mutex(pthread_mutex_t *mutex) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) {
        perror("pthread_mutexattr_init failed");
        return -1;
    }
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_mutexattr_setpshared failed");
        pthread_mutexattr_destroy(&attr);
        return -2;
    }
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
        perror("pthread_mutexattr_settype failed");
        pthread_mutexattr_destroy(&attr);
        return -3;
    }
    if (pthread_mutex_init(mutex, &attr) != 0) {
        perror("pthread_mutex_init failed");
        pthread_mutexattr_destroy(&attr);
        return -4;
    }
    pthread_mutexattr_destroy(&attr);
    return 0;
}

bool parse_int_in_range(const char* str, int* val, int min, int max, const char* arg_name)
{
    char* endptr;
    errno = 0;
    const long parsed = strtol(str, &endptr, 10);

    //check 1: keine zahl oder zeigt endpointer auf char?
    if (*endptr != '\0' || str == endptr)
    {
        fprintf(stderr, "Error in Runner: Invalid %s argument\n", arg_name);
        return false;
    }
    //check 2: potenzieller überlauf?
    if (errno == ERANGE || parsed > INT_MAX || parsed < INT_MIN)
    {
        fprintf(stderr, "Error in Runner: %s argument is out of range\n", arg_name);
        return false;
    }
    //check 3. keine 0 prozesse erlauben
    if (parsed < min || parsed > max)
    {
        fprintf(stderr, "Error in Runner: %s must be in range [%d...%d]\n", arg_name, min, max);
        return false;
    }
    *val = (int)parsed;
    return true;
}

//verbosity ist loglevel 0 1 2 3
void runner_log(const int verbosity, const char* message, const int log_level, const char* log_file)
{
    //logging deaktiviert oder globales log level zu niedrig ist -> abbruch
    if (log_file == NULL || log_level < verbosity)
    {
        return;
    }
    FILE* f = fopen(log_file, "a");
    if (f == NULL)
    {
        fprintf(stderr, "Error in Runner: Could not open log file \n");
        return;
    }

    char time_str[64];
    const time_t now = time(NULL);
    const struct tm* t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%FT%T", t);
    // format: Timestamp Verbosity PID Message
    fprintf(f, "%s %d %d %s\n", time_str, verbosity, getpid(), message);
    fclose(f);
}

bool parse_args(const int argc, char** argv, runner_ctx_t* ctx)
{
    int opt;
    bool e_found = false;
    //syntaktische validierung von flags
    // "p:l:v:e" bedeutet: p, l, v brauchen ein Argument (Doppelpunkt). e braucht keins.
    while ((opt = getopt(argc, argv, "h p:l:v:e")) != -1)
    {
        switch (opt)
        {
        case 'h':
            fprintf(stdout, "-h Hilfe \n"
                    "\t Zeigt diese Hilfe an und beendet das Programm. \n"
                    "-p ProzessCount\n"
                    "\t Anzahl der Prozesse, die gestartet werden sollen. \n"
                    "-l Logfile \n"
                    "\t Pfad zur Logdatei, in die die Logmeldungen geschrieben werden.\n"
                    "-v Verbosity \n"
                    "\t Loglevel/Verbosität als Zahl 0..3, ab der Logmeldungen in die Logdatei \n"
                    "\t geschrieben werden. Wird kein Loglevel angegeben, wird standardmäßig \n"
                    "\t 1 (OSMP_LOG_BIB) verwendet. \n"
                    "-e Executable [Argumente] \n"
                    "\t Name der Executable (ggf. mit Pfad), die von diesem Runner gestartet werden soll. \n"
                    "\t Wichtig: Nach dem Flag -e werden alle folgenden Zeichenketten als Executable \n"
                    "\t mit Argumenten interpretiert. Es wird daher angenommen, dass dieser Parameter \n"
                    "\t immer der letzte Runner-Parameter ist\n");
            exit(0);

        case 'p':
            if (!parse_int_in_range(optarg, &ctx->processcount, 1, INT_MAX, "process count"))
            {
                return false;
            }
            break;
        case 'l':
            ctx->log_file = optarg;
            break;
        case 'v':
            if (!parse_int_in_range(optarg, &ctx->log_level, 0, 3, "log level"))
            {
                return false;
            }
            break;
        case 'e':
            e_found = true;
            break; // Bricht nur das switch-case ab!
        default:
            fprintf(stderr, "Usage: %s -p <count> [-l <logfile>] [-v <level>] -e <executable> [args...]\n", argv[0]);
            return false;
        }

        // wenn -e gefunden wurde, brechen wir auch die while-Schleife ab!
        if (e_found)
        {
            break;
        }
    }
    // optind zeigt jetzt durch den Abbruch exakt auf das erste Element NACH dem "-e" Flag
    if (e_found && optind < argc)
    {
        ctx->exec_argv = &argv[optind];
    }
    // logische validierung von Pflichtflags
    if (ctx->processcount <= 0 || ctx->exec_argv == NULL)
    {
        fprintf(stderr, "Error in Runner: Missing or invalid mandatory arguments (-p and -e).\n");
        return false;
    }
    //standard cases laut skript
    if (ctx->log_file != NULL && ctx->log_level == 0)
    {
        ctx->log_level = 1;
    }
    else if (ctx->log_file == NULL && ctx->log_level > 0)
    {
        ctx->log_file = "osmp_default.log";
    }

    return true;
}

void init_peers(const runner_ctx_t* ctx)
{
    //init werte die einmalig init sein müssen
    ctx->layout->barrier.cycle = 0;
    ctx->layout->barrier.valid = 1; //1 = gut
    ctx->layout->barrier.threshold = ctx->processcount;
    ctx->layout->barrier.barrier_count = ctx->processcount;
    if (init_shared_mutex(&ctx->layout->barrier.mtx_barrier) < 0) {
        perror("failed to initialize barrier mutex\n");
        exit(EXIT_FAILURE);
    }

    ctx->layout->gather_barrier.cycle = 0;
    ctx->layout->gather_barrier.valid = 1;
    ctx->layout->gather_barrier.threshold = ctx->processcount;
    ctx->layout->gather_barrier.barrier_count = ctx->processcount;
    if (init_shared_mutex(&ctx->layout->gather_barrier.mtx_barrier) < 0) {
        perror("failed to initialize gather barrier mutex\n");
        exit(EXIT_FAILURE);
    }


    //conditional shit für general barrier
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&ctx->layout->barrier.cond_barrier, &cond_attr);
    pthread_condattr_destroy(&cond_attr);


    //conditional shit für gather barrier
    pthread_condattr_t gather_cond_attr;
    pthread_condattr_init(&gather_cond_attr);
    pthread_condattr_setpshared(&gather_cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&ctx->layout->gather_barrier.cond_barrier, &gather_cond_attr);
    pthread_condattr_destroy(&gather_cond_attr);

    if (init_shared_mutex(&ctx->layout->mtx_abort) < 0)
    {
        perror("failed to initialize mutex\n");
        exit(EXIT_FAILURE);
    }


    //fülle shm_layout mit standardwerten
    for (int i = 0; i < ctx->processcount; i++)
    {
        ctx->layout->peers[i].peer_state = OSMP_PEER_UNUSED;
        ctx->layout->peers[i].pid = 0;
        ctx->layout->peers[i].rank = i;
        ctx->layout->peers[i].mb_read_idx = 0;
        ctx->layout->peers[i].mb_write_idx= 0;


        if (init_shared_mutex(&ctx->layout->peers[i].mtx_state) < 0)
        {
            ctx->layout->peers[i].peer_state = OSMP_PEER_FAILED;
            perror("failed to initialize mutex\n");
            exit(EXIT_FAILURE);
        }


        if (sem_init(&ctx->layout->peers[i].sem_mb_free, SEM_PROCESS_SHARED, OSMP_MAX_MESSAGES_PROC) == -1) {
            ctx->layout->peers[i].peer_state = OSMP_PEER_FAILED;
            perror("failed to initialize semaphore\n");
            exit(EXIT_FAILURE);
        }
        if (sem_init(&ctx->layout->peers[i].sem_mb_full, SEM_PROCESS_SHARED, 0) == -1) {
            ctx->layout->peers[i].peer_state = OSMP_PEER_FAILED;
            perror("failed to initialize semaphore\n");
            exit(EXIT_FAILURE);
        }
        if (init_shared_mutex(&ctx->layout->peers[i].mtx_mb) < 0) {
            ctx->layout->peers[i].peer_state = OSMP_PEER_FAILED;
            perror("failed to initialize mutex\n");
            exit(EXIT_FAILURE);
        }
        for (int j = 0; j < OSMP_MAX_MESSAGES_PROC; j++) {
            ctx->layout->peers[i].mailbox[j] = -1;
        }


    }
}

void init_msg_slots(const runner_ctx_t* ctx) {
    for (int i = 0; i < OSMP_MAX_SLOTS; i++) {
        ctx->layout->message_slots[i].next_index = i+1;
    }
    ctx->layout->message_slots[OSMP_MAX_SLOTS-1].next_index = -1;
}

bool spawn_peers(const runner_ctx_t* ctx)
{
    //FORK -> EXEC logik
    for (int j = 0; j < ctx->processcount; j++)
    {
        pthread_mutex_lock(&ctx->layout->mtx_abort);
        int abort = ctx->layout->abort;
        pthread_mutex_unlock(&ctx->layout->mtx_abort);
        if (abort == 1) {
            perror("[RUNNER] Fatal: Peer failed to start, stopping creation of other peers\n");
            return false;
        }
        const pid_t res = fork();

        if (res == -1)
        {
            fprintf(stderr, "Error in Runner: fork() \n");
            pthread_mutex_lock(&ctx->layout->mtx_abort);
            ctx->layout->abort = 1;
            pthread_mutex_unlock(&ctx->layout->mtx_abort);
            return false;
        }
        if (res > 0)
        {

        }
        if (res == 0)
        {
            //erstelle kindprozess
            //execvp: vector, path
            // sig: int execvp(const char *file, char *const argv[])
            execvp(ctx->exec_argv[0], ctx->exec_argv);
            pthread_mutex_lock(&ctx->layout->mtx_abort);
            ctx->layout->abort = 1;
            pthread_mutex_unlock(&ctx->layout->mtx_abort);
            //execvp returned nur wenn ein fehler auftritt
            fprintf(stderr, "Error in Runner: execvp failed for %s\n", ctx->exec_argv[0]);
            exit(EXIT_FAILURE);
        }
        //elternprozess
        ctx->layout->peers[j].pid = res; //speichert PID
        if (ctx->layout->peers[j].peer_state == OSMP_PEER_UNUSED)
        {
            ctx->layout->peers[j].peer_state = OSMP_PEER_STARTING; //update via status enum
        }
    }
    return true;
}

void wait_peers(const runner_ctx_t* ctx)
{
    //WAIT logik
    for (int k = 0; k < ctx->processcount; k++)
    {
        int status;
        //warten auf alle kinder durch status 0 = blockierend bis durch ist
        //NUR WARTEN WENN PID VALID IST (failed fork gibt -1 zurück)
        //sig: waitpid(pid_t pid, int *wstatus, int options);
        if (ctx->layout->peers[k].pid > 0)
        {
            waitpid(ctx->layout->peers[k].pid, &status, 0);

            // Evaluierung des Exit-Status (für Termin 1 empfohlen)
            if (WIFEXITED(status))
            {
                const int exit_code = WEXITSTATUS(status);
                char msg[256];
                snprintf(msg, sizeof(msg), "[RUNNER] child process %d (PID: %d) terminated with exit code %d", k,
                         ctx->layout->peers[k].pid, exit_code);
                runner_log(1, msg, ctx->log_level, ctx->log_file);
                //update status im struct
                ctx->layout->peers[k].peer_state = OSMP_PEER_TERMINATED;
            }
        }
    }
}

void cleanup_shm(runner_ctx_t* ctx)
{
    if (ctx->layout != NULL)
    {
        munmap(ctx->layout, ctx->shm_size);
    }

    if (ctx->shm_name[0] != '\0')
    {
        shm_unlink(ctx->shm_name);
    }
}

void abort_and_cleanup(runner_ctx_t *ctx)
{
    runner_log(3, "[RUNNER] Failed to validate all child processes. Initiating Kill all.", ctx->log_level, ctx->log_file);
    //iterier durch unsere peers, hol die pid und rufe kill für die pid auf.
    for (int k = 0; k < ctx->layout->num_peers; k++)
    {
        if (ctx->layout->peers[k].pid > 0)
        {
            kill(ctx->layout->peers[k].pid, SIGTERM);
        }
    }
    //warte bis alle pids gekillt sind.
    for (int k = 0; k < ctx->layout->num_peers; k++)
    {
        if (ctx->layout->peers[k].pid > 0)
        {
            waitpid(ctx->layout->peers[k].pid, NULL, 0);
        }
    }
    //ERST DANACH: cleanup
    cleanup_shm(ctx);
}


int main(const int argc, char** argv)
{
    runner_ctx_t ctx = {0};

    //fehlermeldungen werden durch parse_args direkt geprintet (inklusive defaults)
    if (!parse_args(argc, argv, &ctx))
    {
        return EXIT_FAILURE;
    }
    //start-log und ausgabe
    fprintf(stdout, "Building %d Processes of Executable %s\n", ctx.processcount, ctx.exec_argv[0]);

    char msg_buffer[256];
    snprintf(msg_buffer, sizeof(msg_buffer), "[RUNNER] Starte %d Peers mit Executable '%s'.", ctx.processcount,
             ctx.exec_argv[0]);
    runner_log(1, msg_buffer, ctx.log_level, ctx.log_file);

    //shared memory vorbereitung (name collision avoidance)
    snprintf(ctx.shm_name, OSMP_MAX_SHM_NAME_LENGTH, "/osmp_shm_%d", getpid());

    ctx.layout = setup_shm(ctx.shm_name, ctx.processcount, &ctx.shm_size);
    if (ctx.layout == NULL)
    {
        return EXIT_FAILURE; // shm_unlink() passierte im fehlerfall schon im setup_shm
    }

    // globale variablen ins struct schreiben
    ctx.layout->num_peers = ctx.processcount;
    ctx.layout->abort = 0;
    if (ctx.log_file != NULL)
    {
        strncpy(ctx.layout->log_name, ctx.log_file, OSMP_MAX_LOGNAME_LENGTH - 1);
        ctx.layout->log_name[OSMP_MAX_LOGNAME_LENGTH - 1] = '\0';
    }
    else {
        ctx.layout->log_name[0] = '\0'; // kein Logging
    }
    ctx.layout->verbosity = (OSMP_Verbosity)ctx.log_level;

    // Initialisieren der Semaphore und des Mutex
    if (sem_init(&ctx.layout->sem_global_free, SEM_PROCESS_SHARED, OSMP_MAX_SLOTS) == -1) {
        return EXIT_FAILURE;
    }
    if (sem_init(&ctx.layout->sem_init_done,SEM_PROCESS_SHARED, 0) == -1)
    {
        return EXIT_FAILURE;
    }
    if (sem_init(&ctx.layout->sem_work_start, SEM_PROCESS_SHARED, 0) == -1)
    {
        return EXIT_FAILURE;
    }
    init_shared_mutex(&ctx.layout->mtx_global_free);

    //init the message slots so the linked list is valid
    init_msg_slots(&ctx);

    //peer init mit standartwerten
    init_peers(&ctx);

    // FORK UND EXEC
    if (!spawn_peers(&ctx))
    {
        abort_and_cleanup(&ctx);
        return EXIT_FAILURE;
    }

    //PEER READY CONTROL

    for (int i = 0 ; i< ctx.layout->num_peers; i++)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // 1 Sekunde warten pro Versuch

        const int res = sem_timedwait(&ctx.layout->sem_init_done, &ts);
        if (res == -1)
        {
            if (errno == EINTR)
            {
                i--;
                continue;
            }
            else if (errno == ETIMEDOUT)
            {
                // Timeout erreicht -> checken, ob ein Peer gestorben ist (WNOHANG poll)
                int status;
                pid_t p = waitpid(-1, &status, WNOHANG);
                if (p > 0) {
                    fprintf(stderr, "[RUNNER] Fatal: Peer process %d died unexpectedly during init!\n", p);
                    abort_and_cleanup(&ctx);
                    return EXIT_FAILURE;
                }
                // Niemand ist gestorben, einfach weiter warten
                i--;
                continue;
            }
            //FAIL:
            abort_and_cleanup(&ctx);
            return EXIT_FAILURE;
        }
    }
    for (int i = 0; i < ctx.layout->num_peers; i++) {
        sem_post(&ctx.layout->sem_work_start);
    }


        //WAIT
        wait_peers(&ctx);

        //CLEANUP
        /*
         * auslesen der Logs braucht synchronisation um duplikate zu vermeiden
         */
        runner_log(1, "[RUNNER]: Started Shared Memory cleanup", ctx.log_level, ctx.log_file);

        cleanup_shm(&ctx);

        return EXIT_SUCCESS;
    }
