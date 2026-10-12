/******************************************************************************
 * FILE: OSMP.h
 * AUTHOR: BS-Team
 * DESCRIPTION:
 * Diese Datei beinhaltet alle in der Anleitung angegebenen Prototypen der OSMP
 * Kernfunktionen wie z. B. OSMP_Test() und vorgegebene Konstanten.
 ******************************************************************************/

#ifndef __OSMP_H__
#define __OSMP_H__

/**
 * Alle OSMP-Funktionen liefern im Erfolgsfall OSMP_SUCCESS als Rückgabewert.
 * Im Fehlerfall liefern alle OSMP-Funktionen einen Wert != OSMP_SUCCESS zurück.
 * Ein Fehler muss jedoch nicht zwingend zum Programmende führen.
 * Beispiel: Wenn ein Prozess eine Nachricht an einen nicht existierenden
 * Prozess mit OSMP_Send() schickt, sollte OSMP_Send einen Fehlercode
 * zurückgeben, aber nicht das Programm beenden.
 * Weitere Rückgabewerte können mit Begründung (und Dokumentation!) in einem
 * anderen Header definiert werden, also ohne diesen Header zu verändern.
 */
#define OSMP_SUCCESS 0


/**
 * Die maximale Zahl der Nachrichten pro Prozess
 */
#define OSMP_MAX_MESSAGES_PROC 16

/**
 * Die maximale Anzahl der Nachrichten, die insgesamt vorhanden sein dürfen
 */
#define OSMP_MAX_SLOTS 256

/**
 * Die maximale Länge der Nutzlast einer Nachricht
 */
#define OSMP_MAX_PAYLOAD_LENGTH 1024

/**
 * Maximale Länge des Shared Memory Namens inkl. '\0'
*/
#define OSMP_MAX_SHM_NAME_LENGTH 128

/**
 * Maximale Länge des Logfile Namens inkl. '\0'
*/
#define OSMP_MAX_LOGNAME_LENGTH 128

/**
 * opaker Datentyp für OSMP_Request 
 * */ 
typedef void *OSMP_Request;

/**
 * Die OSMP-Datentypen entsprechen den C-Datentypen.
 * Sie werden verwendet, um den Typ der Daten anzugeben, die mit den
 * OSMP-Funktionen gesendet bzw. empfangen werden sollen.
 */
typedef enum  {
    OSMP_SHORT,           // short int
    OSMP_INT,             // int
    OSMP_LONG,            // long int
    OSMP_UNSIGNED_CHAR,   // unsigned char
    OSMP_UNSIGNED,        // unsigned
    OSMP_UNSIGNED_SHORT,  // unsigned short int
    OSMP_UNSIGNED_LONG,   // unsigned long int
    OSMP_FLOAT,           // float
    OSMP_DOUBLE,          // double
    OSMP_BYTE             // char
} OSMP_Datatype;

/**
 * Die OSMP-Logtypen.
 */
typedef enum {
    OSMP_LOG_NONE,      // -v 0 oder nichts angegeben.
    OSMP_LOG_BIB,       // -v 1 (default)
    OSMP_LOG_SYS,       // -v 2
    OSMP_LOG_ALL,       // -v 3
} OSMP_Verbosity;

/**
 * Gibt den Wert von OSMP_SUCCESS zurück.
 *
 * @return OSMP_SUCCESS
 */
int OSMP_GetSuccess(void);

/**
 * Gibt die maximale Zahl der Nachrichten pro Prozess zurück.
 *
 * @return Die maximale Zahl der Nachrichten pro Prozess
 */
int OSMP_GetMaxMessagesProc(void);

/**
 * Gibt die Maximale Anzahl der Nachrichten, die insgesamt vorhanden sein dürfen
 * zurück.
 *
 * @return Die maximale Anzahl der Nachrichten, die insgesamt vorhanden sein
 * dürfen
 */
int OSMP_GetMaxSlots(void);

/**
 * Gibt die maximale Länge der Nutzlast einer Nachricht zurück.
 *
 * @return Die maximale Länge der Nutzlast einer Nachricht
 */
int OSMP_GetMaxPayloadLength(void);

/**
 * Gibt die maximale Länge des Shared Memory Namens zurück.
 *
 * @return Die maximale Länge des Shared Memory Namens
 */
int OSMP_GetMaxLogNameLength(void);

/**
 * Gibt die maximale Länge des Logfile Namens zurück.
 *
 * @return Die maximale Länge des Logfile Namens
 */
int OSMP_GetMaxLogNameLenght(void);

/**
 * @brief Gibt zur Laufzeit einen eindeutigen Namen für den Shared-Memory-Bereich zurück.
 *
 * Die Funktion erzeugt einen gültigen Namen für den Shared-Memory-Bereich und
 * schreibt diesen in den übergebenen Puffer. Der Name wird **zur Laufzeit** generiert
 * und basiert auf Eigenschaften des aufrufenden Prozesses (z. B. der Prozess-ID),
 * um Kollisionen mit anderen Programminstanzen zu vermeiden.
 *
 * Der erzeugte Name beginnt mit einem '/'-Zeichen und darf die maximale Länge
 * von @ref OSMP_MAX_SHM_NAME_LENGTH nicht überschreiten.
 *
 * Der Puffer muss vom Aufrufer bereitgestellt werden und mindestens
 * @ref OSMP_MAX_SHM_NAME_LENGTH Bytes groß sein. Der zurückgegebene String ist
 * nullterminiert.
 *
 * @param [out] name Zeiger auf den Puffer, in den der erzeugte Name geschrieben wird.
 *
 * @return OSMP_SUCCESS bei Erfolg, ein negativer Wert im Fehlerfall.
 */
int OSMP_GetShmName(char* name);


/**
 * Die Funktion OSMP_SizeOf() liefert in *size* die Größe des Datentyps datatype
 * in Byte zurück.
 *
 * @param [in] datatype OSMP-Datentyp
 * @param [out] size Größe des Datentyps in Byte
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_SizeOf(OSMP_Datatype datatype, unsigned int *size);

/**
 * Die Funktion OSMP_Init() initialisiert die OSMP-Umgebung und ermöglicht den
 * Zugang zu den gemeinsamen Ressourcen der OSMP-Prozesse. Sie muss in jedem
 * OSMP-Prozess vor allen anderen OSMP-Funktionen aufgerufen werden. 
 *
 * @param [in] argc Adresse der Argumentzahl
 * @param [in] argv Adresse des Argumentvektors
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Init(const int *argc, char ***argv);

/**
 * Die Funktion OSMP_Size() liefert in *size* die Zahl der OSMP-Prozesse ohne
 * den OSMP-Starter Prozess zurück. Der in size zurückgegebene Wert sollte mit
 * der Zahl übereinstimmen, die dem OSMP-Starter übergeben wurde.
 *
 * @param [out] size Zahl der OSMP-Prozesse
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Size(int *size);

/**
 * Die Funktion OSMP_Rank() liefert in *rank die OSMP-Prozessnummer des
 * aufrufenden OSMP-Prozesses zurück.
 *
 * @param [out] rank Prozessnummer 0,…,np-1 des aktuellen OSMP-Prozesses
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Rank(int *rank);

/**
 * Die Funktion OSMP_Send() sendet eine Nachricht an den Prozess mit der Nummer
 * dest. Die Nachricht besteht aus count Elementen vom Typ datatype. Die zu
 * sendende Nachricht wird im aufrufenden Prozess ab der Adresse buf abgelegt.
 * Die Funktion ist blockierend, d.h. wenn sie in das aufrufende Programm
 * zurückkehrt, ist der Kopiervorgang abgeschlossen und Speicher buf kann 
 * verändert oder gelöscht werden.
 *
 * @param [in] data     Startadresse des Speichers mit der zu sendenden Nachricht
 * @param [in] count    Zahl der Elemente vom angegebenen Typ im Speicher
 * @param [in] datatype OSMP-Typ der Daten im Speicher
 * @param [in] dest     Nummer des Empfängers zwischen 0,…,np-1
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Send(const void *data, int count, OSMP_Datatype datatype, int dest);

/**
 * Der aufrufende Prozess empfängt eine Nachricht mit maximal count Elementen
 * des angegebenen Datentyps datatype ab der Adresse data. Die OSMP-Prozessnummer
 * des sendenden Prozesses wird unter source und die tatsächliche Länge der gelesenen
 * Nachricht wird unter len abgelegt. 
 * Die Funktion ist blockierend, d.h. sie wartet, bis eine Nachricht für den Prozess 
 * vorhanden ist. Wenn die Funktion zurückkehrt, ist der Kopierprozess abgeschlossen.
 * Die Nachricht gilt nach dem Aufruf dieser Funktion als abgearbeitet.
 *
 * @param [out] data     Startadresse des Puffers im lokalen Speicher des
 * aufrufenden Prozesses, in den die Nachricht kopiert werden soll.
 * @param [in]  count    maximale Zahl der Elemente vom angegebenen Typ, die
 * empfangen werden können
 * @param [in]  datatype OSMP-Typ der Daten im Puffer
 * @param [out] source   Nummer des Senders zwischen 0,…,np-1
 * @param [out] len      tatsächliche Länge der empfangenen Nachricht in Byte
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Recv(void *data, int count, OSMP_Datatype datatype, int *source, int *len);

/**
 * Bevor OSMP-Prozesse enden muß diese Funktion aufgerufen werden, um den Zugriff 
 * auf belegte Ressourcen freizugenen. Hierbei muss jeder Prozess insbesondere alle 
 * noch vorhandenen Nachrichten abarbeiten. Vorher ist der Posteingang zu sperren, um
 * zu verhindern, das dem Prozess noch neue Nachrichten zugesendet werden können.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Finalize(void);

int OSMP_ISend(const void *data, int count, OSMP_Datatype datatype, int dest,
               OSMP_Request request);

/**
 * Die Funktion empfängt eine Nachricht analog zu OSMP_Recv().
 * Die Funktion kehrt jedoch sofort zurück, ohne dass das Kopieren der Nachricht
 * sichergestellt ist (nicht blockierendes Empfangen).
 *
 * @param [out]     data Startadresse des Speicherbereichs, an dem die zu empfangende
 *                  Nachricht gespeichert werden soll.
 * @param [in]      count Zahl der Elemente vom angegebenen Typ, die empfangen
 *                  werden können
 * @param [in]      datatype OSMP-Typ der Daten im Puffer
 * @param [out]     source PID des Senders zwischen 0, …, np-1
 * @param [out]     len tatsächliche Länge der empfangenen Nachricht in Byte
 * @param [in, out] request Adresse einer Datenstruktur, die später verwendet
 * werden kann, um abzufragen, ob die die Operation abgeschlossen ist.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_IRecv(void *data, int count, OSMP_Datatype datatype, int *source,
               int *len, OSMP_Request request);

/**
 * Die Funktion testet, ob die mit der Request verknüpften Operation
 * abgeschlossen ist. Sie ist nicht blockierend, d.h. sie wartet nicht auf das
 * Ende der mit request verknüpften Operation.
 *
 * @param [in]  request Adresse der Struktur, die eine blockierende Operation
 * spezifiziert
 * @param [out] flag Gibt den Status der Operation an.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Test(OSMP_Request request, int *flag);

/**
 * Die Funktion wartet, bis die mit der Request verknüpfte, nicht blockierende
 * Operation abgeschlossen ist. Sie ist so lange blockiert, bis dies der Fall
 * ist.
 *
 * @param [in] request Adresse der Struktur, die eine nicht blockierende
 * Operation spezifiziert
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Wait(OSMP_Request request);

/**
 * Erstellt eine OSMP_Request.
 * Eine OSMP_Request wird dazu verwendet, um nicht blockierende Operationen zu
 * überwachen.
 *
 * @param [out] request Adresse eines Requests (input)
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_CreateRequest(OSMP_Request *request);

/**
 * Löscht eine OSMP_Request.
 *
 * @param [in] request Adresse eines Requests
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_RemoveRequest(OSMP_Request *request);

/**
 * Diese kollektive Funktion blockiert den aufrufenden Prozess.
 * Erst wenn alle anderen Prozesse ebenfalls an der Barriere angekommen sind,
 * laufen die Prozesse weiter.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Barrier(void);

/**
 * Diese Funktion ermöglicht die Gather-Kommunikation.
 * Hierbei können mehrere Prozesse an einen Empfänger Prozess Daten schicken.
 * Es sind alle Prozesse an einem Gather Aufruf beteiligt, ein einzelner ist
 * hierbei Empfänger.
 *
 * @param [in]  sendbuf   Zeiger auf den Sendepuffer.
 * @param [in]  sendcount Anzahl der Elemente im Sendepuffer.
 * @param [in]  sendtype  OSMP-Datentyp der Elemente im Sendepuffer.
 * @param [out] recvbuf   Zeiger auf den Empfangspuffer.
 * @param [in]  recvcount Anzahl der Elemente im Empfangspuffer.
 * @param [in]  recvtype  OSMP-Datentyp der Elemente im Empfangspuffer.
 * @param [in]  root      Rang des Root-Prozesses, der die Daten empfängt.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Gather(void *sendbuf, int sendcount, OSMP_Datatype sendtype,
                void *recvbuf, int recvcount, OSMP_Datatype recvtype, int root);

/**
 * Die Funktion sendet eine Nachricht analog zu OSMP_Send().
 * Die Funktion kehrt jedoch sofort zurück, ohne dass das Kopieren der Nachricht
 * sichergestellt ist (nicht blockierendes Senden).
 *
 * @param [in]      data Startadresse des Speichers mit der zu sendenden Nachricht
 * @param [in]      count Zahl der Elemente vom angegebenen Typ im Puffer
 * @param [in]      datatype  OSMP-Typ der Daten im Puffer
 * @param [in]      dest PID des Empfängers zwischen 0, …, np-1
 * @param [in, out] request Adresse einer eigenen Datenstruktur, die später
 * verwendet werden kann, um abzufragen, ob die Operation abgeschlossen ist.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */

/**
 * Loggt die Nachricht *message* in die entsprechende Logdatei (Parameter: -l),
 * falls die gesetzte Verbosität (Parameter: -v) größer als *verbosity* ist.
 *
 * @param [in] verbosity Level, ab dem die message ausgegeben werden soll.
 * @param [in] message Nachricht, die geloggt werden soll.
 *
 * @return Im Erfolgsfall wird OSMP_SUCCESS zurück gegeben.
 */
int OSMP_Log(OSMP_Verbosity verbosity, char *message);

#endif  // __OSMP_H__
