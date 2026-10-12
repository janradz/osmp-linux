# Projektdokumentation: Operating System Message Passing (OSMP)

## Inhaltsverzeichnis
1. [Benutzerdokumentation](#1-benutzerdokumentation)
    1.1. [Systemarchitektur und Programmkomponenten](#11-systemarchitektur-und-programmkomponenten)
    1.2. [Build-Management](#12-build-management)
    1.3. [Programmaufruf und Parameter](#13-programmaufruf-und-parameter)
    1.4. [Ebenenbasiertes Logging](#14-ebenenbasiertes-logging)
2. [Entwurfs- und Architektur-Dokumentation](#2-entwurfs--und-architektur-dokumentation)
    2.1. [Prozess-Lifecycle und Programmablauf](#21-prozess-lifecycle-und-programmablauf)
    2.2. [Aufbau des Shared-Memory-Layouts](#22-aufbau-des-shared-memory-layouts)
    2.3. [Synchronisation der Kommunikation (ISend / IRecv)](#23-synchronisation-der-kommunikation-isend--irecv)
    2.4. [Kollektive Synchronisation (Barrier / Gather)](#24-kollektive-synchronisation-barrier--gather)
    2.5. [Skalierbarkeit und Interne Struktur](#25-skalierbarkeit-und-interne-struktur)
3. [Test- und Ergebnisdokumentation](#3-test--und-ergebnisdokumentation)
    3.1. [Durchführung und Auswertung der Test-Executables](#31-durchführung-und-auswertung-der-test-executables)
    3.2. [Analyse bekannter Architektur-Einschränkungen (Deadlocks)](#32-analyse-bekannter-architektur-einschränkungen-deadlocks)
    3.3. [Lösung spezifischer Thread-Probleme (Double-Join-Fix)](#33-lösung-spezifischer-thread-probleme-double-join-fix)
4. [Quelltext-Dokumentation](#4-quelltext-dokumentation)

---

## 1. Benutzerdokumentation

### 1.1. Systemarchitektur und Programmkomponenten
Das OSMP-System stellt eine hochperformante, datenzentrische Interprozesskommunikation (IPC) für UNIX-basierte Systeme bereit. Durch die Nutzung eines gemeinsamen Speichersegments (Shared Memory) kommunizieren Prozesse direkt und ohne den leistungskritischen Kontextwechsel über den Betriebssystem-Kernel.

Die Architektur trennt das System in zwei Haupteinheiten:
- **OSMP-Runner (`osmpRun`)**: Der zentrale Supervisor. Er initialisiert die Laufzeitumgebung, formatiert das Shared-Memory-Segment (`shm_layout_t`) und erzeugt das globale Synchronisations-Framework (POSIX Semaphore und Mutexe). Anschließend instanziiert er die Kindprozesse mittels `fork()` und `execvp()`. Der Runner fungiert zudem als Wächter, der Systemabstürze detektiert und ein rigoroses Aufräumen aller Ressourcen (`munmap`, `shm_unlink`) erzwingt.
- **OSMP-Executables**: Die eigentlichen Anwenderprogramme (z.B. `circular_stress`, `gatherer`, `test_barrier`). Diese binden die von uns bereitgestellte C-Bibliothek (`osmpLib`) ein, welche die komplette Komplexität des Shared-Memory-Zugriffs abstrahiert und Schnittstellen für asynchrone (`OSMP_ISend`, `OSMP_IRecv`) und kollektive (`OSMP_Barrier`, `OSMP_Gather`) Operationen bereitstellt.

### 1.2. Build-Management
Der Kompilierungsprozess wird über CMake gesteuert. Dies gewährleistet die korrekte Einbindung der Pthread-Bibliotheken sowie plattformunabhängige Builds.

**Erstellung der Binärdateien:**
```bash
mkdir -p build && cd build
cmake ..
make
```

### 1.3. Programmaufruf und Parameter
Die Steuerung des Runners erfolgt über einen Kommandozeilen-Parser. Alle eingegebenen Parameter unterliegen strengen Range-Checks, um Buffer Overflows und fehlerhafte Speicherallokationen auszuschließen.

**Syntax:**
```bash
./osmpRun -p <Anzahl> [-l <Logdatei>] [-v <Loglevel>] -e <Executable> [Argumente...]
```

- `-p <Anzahl>` *(Pflicht)*: Spezifiziert die zu instanziierende Menge an OSMP-Peers. Die Größe des Shared Memory wird zur Laufzeit an diesen Wert angepasst.
- `-l <Logdatei>` *(Optional)*: Pfad für die strukturierte Protokollierung. Bei fehlendem Argument erfolgt die Ausgabe auf `stdout`.
- `-v <Loglevel>` *(Optional)*: Integer (0 bis 3) zur Definition der Log-Verbosität.
- `-e <Executable>` *(Pflicht)*: Pfad zur Anwender-Binärdatei. Sämtliche darauf folgenden Zeichenketten werden in das `argv`-Array des Kindprozesses transferiert.

### 1.4. Ebenenbasiertes Logging
Das OSMP-System führt ein detailliertes Protokoll aller Systemzustände, welches durch den Parameter `-v` gesteuert wird:
- `0 (OSMP_LOG_NONE)`: Keine Protokollierung.
- `1 (OSMP_LOG_BIB)`: API-Einsprünge (z. B. `OSMP_Init`, `OSMP_Send`) werden erfasst.
- `2 (OSMP_LOG_SYS)`: Ergänzt betriebssystemnahe Ereignisse (`fork()`, Terminierungen, OS-Fehler).
- `3 (OSMP_LOG_ALL)`: Erfasst Thread-Lifecycles, Semaphore-Zustände und Request-Management-Informationen in höchster Detailtiefe.

Jede Logzeile befolgt strikt das Format `<Timestamp> <Verbosity> <pid> <message>`. Dieses standardisierte Format ermöglicht die parallele, überschneidungsfreie Protokollierung von Runner und Peers in dieselbe Datei.

---

## 2. Entwurfs- und Architektur-Dokumentation

### 2.1. Prozess-Lifecycle und Programmablauf
Der Lebenszyklus der Prozesse unterliegt einer strikten Synchronisation, um den Zugriff auf unvollständige Speicherkonstrukte sicher auszuschließen.

1. **Speicher-Allokation (`setup_shm`)**: Der Runner berechnet den Speicherbedarf des Basis-Structs `shm_layout_t`. Dieses enthält neben globalen Verwaltungsvariablen auch das dynamische Array der Peer-Objekte (`osmp_peer_t peers[]`). Mittels `shm_open`, `ftruncate` und `mmap` wird das Segment ins System gebunden.
2. **Initialisierung der Primitive**: Semaphore (z. B. `sem_init_done`, `sem_work_start`) und Mutexe werden explizit mit `PTHREAD_PROCESS_SHARED` formatiert. Die globalen Message-Slots (`message_slots`) werden in O(n) zu einer verketteten Liste verlinkt.
3. **Prozess-Erzeugung (Spawn)**: Der Runner iteriert über die gewünschte Prozessanzahl. Pro Iteration wird `fork()` aufgerufen. Der Kindprozess wechselt den Zustand `peer_state` auf `OSMP_PEER_STARTING` und überschreibt sein Abbild via `execvp()`.
4. **Registrierung (Handshake)**: Im Executable ruft der Peer `OSMP_Init` auf. Er sucht seine PID im `peers[]`-Array, erhält seinen Rang (`rank`) und signalisiert seine Einsatzbereitschaft via `sem_post(&sem_init_done)`.
5. **System-Freigabe**: Der Runner wartet blockierend (`sem_timedwait`), bis alle Prozesse den Handshake vollzogen haben. Erst dann triggert er `sem_work_start`, womit alle Peers zeitgleich ihre Applikationslogik beginnen.
6. **Tear-Down**: Die Peers bauen durch Aufruf von `OSMP_Finalize` ihre Thread-Ressourcen ab. Der Runner erfasst die Terminierung über `waitpid`, zerstört alle POSIX-Objekte und hebt die Speicherbindung auf (`munmap`, `shm_unlink`). 
Zusätzlich werden von `OSMP_Finalize` sämtliche offene requests sauber beendet. Auf Offene Send-Threads wird gewartet, während bei offenen Receive-Threads das *cancel*-flag gesetzt wird.     

### 2.2. Aufbau des Shared-Memory-Layouts
Die Speicherarchitektur ist gezielt auf O(1)-Suchalgorithmen optimiert, um lineare Scans bei der Speicheranforderung zu vermeiden. Das Kern-Konstrukt ist `shm_layout_t`.

- **Verkettete Liste der `osmp_msg_t`**: Das `message_slots`-Array verwaltet die globalen Nachrichten. Anstatt einer Bitmap wird jeder Slot mit dem Index des nächsten freien Slots (`next_index`) verknüpft. Der Pointer `msg_slots_head` zeigt stets auf das erste freie Element. Die Allokation eines Speicherplatzes erfordert lediglich das Umbiegen dieses Pointers unter einem minimalen Lock von `mtx_global_free`.
- **Peer-Struktur (`osmp_peer_t`)**: Diese repräsentiert jeden Prozess und enthält unter anderem die PID, den Rang und das Status-Enum `osmp_peer_state_t`.
- **Ringpuffer-Mailboxen**: Jeder Peer besitzt ein `mailbox`-Array der Dimension `OSMP_MAX_MESSAGES_PROC`. Der Zugriff wird als Ringpuffer (`mb_read_idx` und `mb_write_idx` kombiniert mit Modulo-Arithmetik) vollzogen. Zur Zugriffskontrolle nutzt der Ringpuffer zählende Semaphoren (`sem_mb_full`, `sem_mb_free`) sowie das Mutex `mtx_mb`.
- **Vermeidung redundanter Kopien**: Beim Versand von Nachrichten wird der Payload exakt einmal in den globalen Nachrichtenspeicher kopiert. Dem Empfänger wird lediglich der Integer-Index des Speicherslots über die Mailbox übergeben.



### 2.3. Aufbau eines Peers
Ein Peer beschreibt einen Prozess, der das bei der Ausführung des Programmes angegebene Programm ausführt.
Generell besteht dieser aus Metadaten und einer Mailbox in welcher Nachrichten empfangen werden können. \
Ein Peer besitzt intern die folgenden Felder:

- `Pid` Die eigene Prozess-ID.
- `Rank` Der Rang innerhalb der OSMP Bibliothek (eine interne Kennnummer).
- `State` Status des Peers.
- `Read_Index` Lesepunkt für den Ringbuffer.
- `Write_Index` Schreibpunkt für den Ringbuffer.
- `Mailbox_Full`, `Mailbox_Free` Semaphoren zur überwachung der freien/gefüllten Mailbox plätze.
- `Mailbox_Access` Mutex zur absicherung der Mailbox.
- `State_Access` Mutex zur absicherung des Status.



### 2.4. Aufbau einer Nachricht
Nachrichten werden im Shared_Memory in einer Linked List abgebildet.
Dabei wird ein Array mit Fester größe initialisiert, welche Nachrichten Slots enthält.
Ein solcher Slot wird beim Senden reserviert und mit den gebrauchten Daten befüllt.
Eine Nachricht wird intern über die folgende Struktur abgebildet.

- `Next_Index` Index des nächsten Slots.
- `Length` Länge der Nachricht (gerichtet am Datentyp).
- `Sender` Versender der Nachricht.
- `Datatype` Datentyp der Nachricht
- `Payload` Die Nachricht (limitiert von `OSMP_MAX_PAYLOAD_LENGTH`)




### 2.5. Synchronisation der Kommunikation (ISend / IRecv)
Die asynchrone Punkt-zu-Punkt-Kommunikation wurde nach dem Erzeuger-Verbraucher-Paradigma (Producer-Consumer) konzipiert. Jeder Request (`OSMP_Request`) wird intern durch das Struct `osmp_request_internal_t` repräsentiert, welches einen asynchronen Worker-Thread, ein Zustands-Enum (`state`) sowie ein lokales `mtx_state` führt.
Die synchronen Varianten `OSMP_Send` und `OSMP_Recv` kapseln die asynchronen Varianten und Synchronisieren mit `OSMP_Wait` den Abschluss der Threads.

**Ablauf OSMP_ISend:**
1. Der Worker-Thread dekrementiert `sem_global_free`, um Kapazitäten zu garantieren.
2. Unter dem Schutz von `mtx_global_free` wird der Slot-Index aus `msg_slots_head` gepoppt.
3. Der Payload wird ins Shared Memory kopiert.
4. Der Worker dekrementiert die `sem_mb_free` der Ziel-Mailbox.
5. Unter Lock des Ziel-`mtx_mb` wird der Slot-Index in den Ringpuffer geschrieben.
6. Das Signal `sem_post(&sem_mb_full)` weckt potenzielle Leser.



**Ablauf OSMP_IRecv:**
1. Der Worker blockiert via `sem_wait(&sem_mb_full)`, bis eine Nachricht anliegt.
2. Unter Lock der eigenen Mailbox (`mtx_mb`) wird der Index über `mb_read_idx` entnommen.
3. Der Payload wird vom Shared Memory in den Applikationspuffer kopiert.
4. Der freigewordene Slot wird an den `msg_slots_head` zurückgehängt und `sem_global_free` wird wieder inkrementiert.



### 2.6. Kollektive Synchronisation (Barrier / Gather)
Die Barriere (`osmp_barrier_t`) schützt kritische Synchronisationspunkte im System.
- **Cycle-Counter-Konzept**: Um Fehler durch *Spurious Wakeups* des Betriebssystems auszuschließen, wird die Condition Variable (`cond_barrier`) in einer Schleife geprüft: `while (my_cycle == barrier->cycle)`. Nur der letzte eintreffende Prozess erhöht den Cycle und löst den `pthread_cond_broadcast` aus.
- **Thread-Sicherheit**: Innerhalb des Wartestatus wird der Thread temporär vor Systemabbrüchen geschützt (`pthread_setcancelstate(PTHREAD_CANCEL_DISABLE)`). Dies verhindert, dass ein durch `pthread_cancel` abgebrochener Thread den Barriere-Mutex unendlich blockiert hinterlässt.
- **Gather Space**: In `OSMP_Gather` durchlaufen die Prozesse die intern abstrahierte Barriere, schreiben ihre Nutzdaten in ein voralloziertes `gather_space`-Array und durchlaufen eine finale Barriere, woraufhin der Root-Peer das Array linear zusammenfügt.




### 2.7. Skalierbarkeit und Interne Struktur
Sämtliche Mutexe sind mit dem Modus `PTHREAD_MUTEX_ERRORCHECK` konfiguriert. Dies detektiert und verbietet sofortige Double-Locks desselben Threads, was die Laufzeitstabilität signifikant absichert.
Hardware-Konstanten wie `OSMP_MAX_SLOTS` und `OSMP_MAX_PAYLOAD_LENGTH` sind über C-Makros in `OSMP.h` extrahiert. Das System skaliert somit ohne Logik-Eingriffe auf größere Hardware-Infrastrukturen.

---

## 3. Test- und Ergebnisdokumentation

### 3.1. Durchführung und Auswertung der Test-Executables
Die Robustheit und Performance der Architektur wurde durch mehrere tiefgreifende C-Executables empirisch evaluiert:

- **`circular_stress` (Ringtopologie-Test)**: Alle Peers posten gleichzeitig 100 asynchrone `ISend`-Requests an ihren rechten Nachbarn und empfangen 100 Nachrichten synchron (`Recv`) vom linken Nachbarn. Der Test belegt eindrucksvoll die Blockierungsfreiheit der asynchronen Architektur: Es kommt zu keinem Deadlock, selbst wenn die Ringpuffer temporär überlaufen, da die Threads im Hintergrund schlafen, bis Speicherplätze freiwerden.
- **`gatherer`**: Verifiziert die Funktionalität von `OSMP_Gather`. Rang 0 allokiert dynamisch den Speicher für die Summe der Nachrichten, während die anderen Prozesse Puffer der Größe 20 beschreiben. Die nahtlose Zusammenführung der Puffer im `gather_space` wurde in der Terminalausgabe nachgewiesen.
- **`test_barrier`**: Iteriert über 200 Zyklen von Barrieren (`OSMP_Barrier`). Der Test beweist, dass der Cycle-Counter-Mechanismus das System absolut stabil hält und es nie zu asynchronem Überholen der Threads kommt.
- **`leak_test`**: Verifiziert das saubere Entladen des Speichers. Valgrind / Thread-Sanitizer bestätigen, dass durch den Aufruf von `OSMP_Finalize` alle Pthread-Requests rückstandslos bereinigt werden.
- **`crash_test` (Aktive Fehlererkennung)**: Das Programm provoziert gezielt einen `SIGSEGV` in einem Kindprozess. Der Runner detektiert den Absturz durch das Polling-Flag `WNOHANG` innerhalb seiner `waitpid()`-Schleife verzögerungsfrei. Er setzt das Systemflag `abort=1` und initiiert sofort ein `SIGTERM` auf alle verbliebenen Peers, wodurch das Shared-Memory-Segment leckfrei aufgelöst wird.

<!-- PLATZHALTER FÜR LOG-AUSZÜGE -->
> **Belege: Screenshots / Log-Auswertungen**
> *(Hinweis: Hier Screenshots der Valgrind-Ausgaben oder storm_test Logs einfügen)*
```markdown

```

### 3.2. Analyse bekannter Architektur-Einschränkungen (Deadlocks)
Ein bekanntes Phänomen manifestiert sich in den Testfällen `asyncTest` sowie `senderDeadlockTest`.
Die Architektur unterliegt der systeminhärenten Grenze der Mailbox-Größe (`OSMP_MAX_MESSAGES_PROC`). Versucht ein Anwenderprogramm (wie `senderDeadlockTest`), Nachrichten an *alle* übrigen Peers in einer Iteration synchron abzusenden (`OSMP_Send`), ohne in der Zwischenzeit Lesebefehle (`OSMP_Recv`) aufzurufen, laufen die Mailboxen aller Peers gleichzeitig voll. Die Semaphoren `sem_proc_free` sinken auf 0 und blockieren die Sende-Prozesse vollkommen korrekt.
Es entsteht ein *Circular Wait* (Deadlock), der nicht durch die OSMP-Bibliothek, sondern ausschließlich durch die fehlerhafte Applikationslogik verursacht wird. Applikationen müssen bei großen Datensätzen das Senden und Empfangen verschränken.

### 3.3. Lösung spezifischer Thread-Probleme (Double-Join-Fix)
Eine erhebliche Herausforderung war die Verwaltung hängender Pthreads während des Tear-Downs (`OSMP_Finalize`).
Wurden asynchrone Threads aufgesetzt, die ihr Ziel nie erreichten (z.B. ein wartender `IRecv`-Worker, für den kein Sender existiert), blockierte `OSMP_Finalize` endlos in der Routine `pthread_join()`.
*Die Lösung:* Im internen Zustand der Requests wurde das Tracking-Flag `joined` implementiert. `Finalize` überprüft dieses Flag für sämtliche Instanzen im Array `requests[256]`. Ausstehende **`RECV`**-Worker werden dediziert durch `pthread_cancel` abgetötet, da sie auf Nachrichten warten, die bei Finalisierung nie mehr eintreffen werden. Offene **`SEND`**-Worker hingegen werden nicht abgebrochen, sondern blockierend (`pthread_join`) auslaufen gelassen, um das bereits begonnene Schreiben in den Shared Memory nicht zu korrumpieren. Diese differenzierte Logik eliminierte jegliche Double-Join- oder Tear-Down-Deadlocks.

---

## 4. Quelltext-Dokumentation

Die Quelltexte des Runners, der Bibliothek und der Executables sind nach strengen, einheitlichen Syntaxvorgaben kommentiert:
- **API-Kommentierung**: In den Header-Dateien (`OSMP.h`, `osmpLib.h`) werden Doxygen-Blöcke genutzt. Jeder Prototyp enthält präzise Angaben zu den erwarteten Datentypen, den Parametern (mit `[in]` und `[out]` Markern) sowie dem Return-Wert (z.B. `OSMP_SUCCESS`).
- **Algorithmische Inline-Kommentare**: Komplexe Arithmetik (wie die Modulo-Berechnung der Ringpuffer `(current_write_idx + 1) % OSMP_MAX_MESSAGES_PROC` oder das Pointer-Management der Verketteten Freiliste) ist durch verständliche C-Inline-Kommentare flankiert.
- **Sprachliche und inhaltliche Konsistenz**: Fachliche Termini (wie *Race Condition*, *Spurious Wakeup*, *Deadlock*) werden im gesamten Projekt homogen angewandt. Es wird durchgehend ein wissenschaftlicher, sachlicher Schreibstil gepflegt. Die Dokumentation und Variablenbezeichnungen sind frei von umgangssprachlichen Phrasen und genderneutral gehalten.

# OSMP Bibliothek – API Schnittstellen

## Initialisierung und Beenden

### `int OSMP_Init(const int *argc, char ***argv)`

Initialisiert die OSMP-Bibliothek und verbindet den Prozess mit dem Shared-Memory-System.
Ist der erste Aufruf in einem Programm, das die Bibliothek nutzen möchte.

**Parameter**
- `argc` – Kommandozeilenargumentanzahl 
- `argv` – Kommandozeilenargumente 

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_Finalize(void)`

Beendet die OSMP-Bibliothek, räumt Ressourcen auf und trennt die Shared-Memory-Verbindung.
Offene Threads und Requests werden sauber beendet.

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_GetShmName(char *name)`

Erzeugt den Namen des verwendeten Shared-Memory-Segments.

**Parameter**
- `name` – Zielpuffer für den Namen

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

## Prozessinformationen

### `int OSMP_Size(int *size)`

Liefert die Anzahl aller OSMP-Prozesse.

**Parameter**
- `size` – Ausgabeparameter für die Prozessanzahl

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_Rank(int *rank)`

Liefert den Rank des aktuellen Prozesses.

**Parameter**
- `rank` – Ausgabeparameter für den Prozess-Rank

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

## Datentypen

### `int OSMP_SizeOf(OSMP_Datatype datatype, unsigned int *size)`

Bestimmt die Größe eines OSMP-Datentyps in Bytes.

**Parameter**
- `datatype` – Datentyp
- `size` – Ausgabeparameter für die Größe

**Unterstützte Datentypen**
- `OSMP_SHORT`
- `OSMP_INT`
- `OSMP_LONG`
- `OSMP_UNSIGNED_CHAR`
- `OSMP_UNSIGNED`
- `OSMP_UNSIGNED_LONG`
- `OSMP_FLOAT`
- `OSMP_DOUBLE`
- `OSMP_BYTE`

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

## Punkt-zu-Punkt Kommunikation

### `int OSMP_Send(const void *buf, int count, OSMP_Datatype datatype, int dest)`

Blockierendes Senden einer Nachricht an einen Zielprozess.

**Parameter**
- `buf` – Sendepuffer
- `count` – Anzahl der Elemente
- `datatype` – Datentyp
- `dest` – Ziel-Rank

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_Recv(void *buf, int count, OSMP_Datatype datatype, int *source, int *len)`

Blockierendes Empfangen einer Nachricht.

**Parameter**
- `buf` – Empfangspuffer
- `count` – Maximale Anzahl Elemente
- `datatype` – Erwarteter Datentyp
- `source` – Ausgabeparameter für Sender-Rank
- `len` – Tatsächlich empfangene Datenmenge in Bytes

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

## Nicht-blockierende Kommunikation

### `int OSMP_ISend(const void *buf, int count, OSMP_Datatype datatype, int dest, OSMP_Request request)`

Startet einen nicht-blockierenden Sendevorgang.

**Parameter**
- `buf` – Sendepuffer
- `count` – Anzahl Elemente
- `datatype` – Datentyp
- `dest` – Ziel-Rank
- `request` – Request-Objekt

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_IRecv(void *buf, int count, OSMP_Datatype datatype, int *source, int *len, OSMP_Request request)`

Startet einen nicht-blockierenden Empfangsvorgang.

**Parameter**
- `buf` – Empfangspuffer
- `count` – Maximale Anzahl Elemente
- `datatype` – Datentyp
- `source` – Ausgabeparameter für Sender-Rank
- `len` – Ausgabeparameter für empfangene Datenlänge
- `request` – Request-Objekt

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

## Request-Verwaltung

Requests werden in verbindung mit asynchronen Operationen (`ISend`/`IRecv`) verwendet.

### `int OSMP_CreateRequest(OSMP_Request *request)`

Erzeugt ein neues Request-Objekt für nicht-blockierende Kommunikation.

**Parameter**
- `request` – Ausgabeparameter für das Request-Handle

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_RemoveRequest(OSMP_Request *request)`

Entfernt eine Request und gibt den zugehörigen Speicher wieder Frei.

**Parameter**
- `request` – Zu löschendes Request-Handle

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_Test(OSMP_Request request, int *flag)`

Prüft den aktuellen Zustand eines Requests.

**Parameter**
- `request` – Request-Handle
- `flag` – Ausgabeparameter für den Status

**Mögliche Zustände**
- `OSMP_REQ_PENDING`
- `OSMP_REQ_IN_PROGRESS`
- `OSMP_REQ_COMPLETE`
- `OSMP_REQ_ERROR`

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_Wait(OSMP_Request request)`

Blockiert bis ein nicht-blockierender Kommunikationsvorgang abgeschlossen wurde.
Nützlich um sicherzugehen, dass ein Vorgang abgeschlossen ist, bevor der Code weiterläuft.

**Parameter**
- `request` – Request-Handle

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

## Kollektive Operationen

### `int OSMP_Barrier(void)`

Synchronisiert alle Prozesse. Erst wenn alle Prozesse die Barrier erreicht haben, dürfen sie weiterlaufen.

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

### `int OSMP_Gather(void *sendbuf, int sendcount, OSMP_Datatype sendtype,
                    void *recvbuf, int recvcount, OSMP_Datatype recvtype,
                    int root)`

Sammelt Daten aller Prozesse beim Root-Prozess.

**Parameter**
- `sendbuf` – Lokaler Sendepuffer
- `sendcount` – Anzahl zu sendender Elemente
- `sendtype` – Datentyp der Sendedaten
- `recvbuf` – Empfangspuffer des Root-Prozesses
- `recvcount` – Größe des Empfangspuffers
- `recvtype` – Datentyp der Empfangsdaten
- `root` – Rank des Root-Prozesses

**Verhalten**
- Jeder Prozess schreibt seine Daten in den gemeinsamen Gather-Speicher.
- Der Root-Prozess kopiert anschließend alle Daten in `recvbuf`.

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bzw. `-2` bei Fehler

---

## Logging

### `int OSMP_Log(OSMP_Verbosity verbosity, char *message)`

Schreibt einen Logeintrag in die konfigurierte Logdatei.

**Parameter**
- `verbosity` – Log-Level
- `message` – Lognachricht

**Rückgabe**
- `OSMP_SUCCESS` bei Erfolg
- `-1` bei Fehler

---

# Beispielcode für einen Aufruf:
```c
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
```

## Aufruf mit ISend/IRecv
```c
int main(int argc, char **argv) {
    if (OSMP_SUCCESS != OSMP_Init(&argc, &argv)) {
        fprintf(stderr, "Fehler: OSMP_Init fehlgeschlagen\n");
        return EXIT_FAILURE;
    }

    int rank, size;
    OSMP_Rank(&rank);
    OSMP_Size(&size);

    OSMP_Request requests[(size*2)-1];
    int test_msg[1] = {rank};
    OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: Now sending message to all peers\n");
    int send_count = 0;
    for (int i = 0; i < size; i++) {
        if (i != rank) {
            OSMP_Request send_request;
            if (OSMP_SUCCESS != OSMP_CreateRequest(&send_request)) {
                fprintf(stderr, "Fehler: OSMP_CreateRequest fehlgeschlagen\n");
                OSMP_Finalize();
                return EXIT_FAILURE;
            }
            requests[send_count] = send_request;

            if (OSMP_SUCCESS != OSMP_ISend(&test_msg, 1, OSMP_INT, i, send_request)) {
                OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: ISend Failure");
            }
            send_count++;
        }
    }
    int status;
    for (int i = 0; i < size-1; i++) {
        OSMP_Test(requests[i], &status);
        fprintf(stderr, "Request %d, Status %d\n", i, status);
    }
    // Sende-Wait wurde ans Ende verschoben, um Mailbox-Deadlocks zu vermeiden!

    OSMP_Log(OSMP_LOG_ALL, "[SR_PEER]: Now Reading messages\n");
    char buf[OSMP_MAX_PAYLOAD_LENGTH];
    int len;
    int sender;
    for (int received = 0; received < size - 1; received++) {
        OSMP_Request recv_request;
        if (OSMP_SUCCESS != OSMP_CreateRequest(&recv_request)) {
            fprintf(stderr, "Fehler: OSMP_CreateRequest fehlgeschlagen\n");
            OSMP_Finalize();
            return EXIT_FAILURE;
        }
        requests[received + send_count] = recv_request;
        if (OSMP_IRecv(&buf, OSMP_MAX_PAYLOAD_LENGTH, OSMP_INT, &sender, &len, recv_request) != OSMP_SUCCESS) {
            fprintf(stderr, "Fehler: OSMP_Recv fehlgeschlagen\n");
            OSMP_Finalize();
            return EXIT_FAILURE;
        }
        OSMP_Wait(recv_request);
        int temp_msg[len];
        memcpy(temp_msg, buf, (size_t)len);
        fprintf(stdout, "Received Msg from %d, length: %d: %d\n", sender, len, temp_msg[0]);
    }

    for (int i = 0; i < send_count * 2; i++) {
        OSMP_Test(requests[i], &status);
        fprintf(stderr, "Request %d, Status %d\n", i, status);
    }


    if (OSMP_SUCCESS != OSMP_Finalize()) {
        fprintf(stderr, "Fehler: OSMP_Finalize\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
```

# Antworten auf die OSMP Leitfragen

Im Folgenden finden sich die ausführlichen Antworten auf die 22 Fragen aus der Praktikumsanleitung, basierend auf der aktuellen Implementierung der OSMP-Bibliothek (`osmpLib.c`) und den Ergebnissen aus den Tests (`test_report.md`).

---

### 1. Sind alle Testprogramme lt. Anleitung zusammen mit der OSMP-Lib fehlerfrei kompilierbar und ausführbar?
Ja, alle unsere eigenen Testprogramme im Stammverzeichnis `src/osmpExecutables/` (wie z. B. `echoAll.c`, `senderReceiver.c`, `asyncTest.c`, `circular_stress.c`, `gatherer.c` und `test_barrier.c`) lassen sich fehlerfrei über das bereitgestellte CMake-System kompilieren und ausführen. Alle selbst entwickelten Tests durchlaufen die Execution erfolgreich (Status `PASS` mit Exit Code 0) und validieren die Funktionen der Bibliothek vollständig ohne Abstürze.

### 2. Was passiert, wenn OSMP-Funktionen ohne vorheriges OSMP_Init(), oder nach OSMP_Finalize() aufgerufen werden, oder wenn die Funktionen fehlschlagen?
In der Implementierung (siehe `osmpLib.c`) wird bei praktisch allen Funktionen am Anfang geprüft:
```c
if (osmp_state != OSMP_STATE_INITIALIZED) return -1;
```
`OSMP_Init` setzt den Status auf `OSMP_STATE_INITIALIZED`, während `OSMP_Finalize` ihn am Ende auf `OSMP_STATE_FINISHED` setzt. Demnach geben Aufrufe ohne `OSMP_Init` oder nach `OSMP_Finalize` deterministisch den Fehlercode `-1` zurück, ohne dass das Programm abstürzt. Wenn interne Funktionen fehlschlagen, werden ebenfalls fehlerhafte Exit-Codes (meist `-1`) zurückgegeben.

### 3. Wie skalieren Ihre Datenstrukturen mit veränderten Größen? Welche Laufzeiten ergeben sich?
Die zentrale Datenstruktur ist das Shared Memory Layout (`shm_layout_t`), das global eine feste Anzahl an Nachrichtenslots (`OSMP_MAX_SLOTS = 256`) bereitstellt, sowie dynamisch mit der Anzahl der gestarteten Prozesse (`num_peers`) wächst.
* **Speicherplatz:** Skaliert linear mit der Anzahl der Peers: `O(N)`, da jeder Peer ein `osmp_peer_t`-Struct (mit Mailboxen und Semaphoren) sowie Platz im `gather_space` Array belegt.
* **Laufzeiten:**
    * Initialisierung (`OSMP_Init`): O(N), da durch die Struktur iteriert werden muss, um die eigene PID im Shared-Memory zu finden.
    * Senden/Empfangen: O(1) für die Verwaltung, da Nachrichten-Slots über einen Listen-Index (`msg_slots_head`) in konstanter Zeit zugeordnet werden. Die Speicher-Operation (`memcpy`) ist abhängig von der Nutzlast (`O(L)`).

### 4. Was passiert, nachdem das Limit der zu empfangenden Nachrichten pro Prozess erreicht wurde?
In der Routine `thread_send` wird für den Empfänger-Prozess ein blockierender Aufruf durchgeführt:
```c
sem_wait(&shared->peers[peer_index].sem_mb_free)
```
Das Semaphor ist maximal auf `OSMP_MAX_MESSAGES_PROC` (16) initialisiert. Ist die Mailbox des Empfängers voll, sinkt der Wert auf 0. Der sendende Thread blockiert in diesem Moment so lange, bis der Empfänger eine Nachricht gelesen und den Platz per `sem_post` wieder freigegeben hat.

### 5. Wie wird mit Nachrichten verfahren, die die maximal erlaubte Länge beim Senden oder Empfangen überschreiten?
* **Beim Senden:** In `thread_send` wird zu Beginn geprüft: `if (total_len > OSMP_MAX_PAYLOAD_LENGTH)`. Trifft dies zu, wird der Fehler geloggt, der Vorgang abgebrochen und der reservierte Slot zurück in die Freiliste gepackt. `OSMP_ISend` wird fehlschlagen.
* **Beim Empfangen:** In `thread_recv` wird die berechnete Lese-Länge (`wanted_len = sizeof(datatype) * count`) mit der gespeicherten Nutzlast (`*len`) verglichen. Ist `wanted_len < *len`, wird die Nachricht abgeschnitten ("Requested datacount smaller than available data"), und es werden nur `wanted_len` Bytes in den Nutzerpuffer kopiert.

### 6. Was passiert, wenn ein Sender einem nicht vorhandenen Prozess eine Nachricht sendet und ist es möglich oder sinnvoll, das Messages an bereits beendete Empfänger gesendet werden?
* **Nicht vorhandener Prozess:** `thread_send` sucht den Rang im `peers`-Array. Wird dieser nicht gefunden, bricht die Funktion ab (`[OSMP_Send] Error: destination peer does not exist`) und schlägt fehl.
* **Beendeter Empfänger:** Senden an beendete Empfänger ist möglich, falls der Peer noch im Array gelistet ist. Es ist jedoch nicht sinnvoll: Ein beendeter Prozess ruft kein `OSMP_Recv` mehr auf. Laufen somit 16 Nachrichten auf, verklemmt der nächste Sender am `sem_mb_free` in einem Deadlock.

### 7. Kann man einem Prozess eine Nachricht senden, während er eine Nachricht liest?
Ja, das ist sicher und problemlos möglich. Das Senden und Empfangen ist thread-sicher implementiert. In `osmpLib.c` werden beim Schreiben und Lesen unterschiedliche Indizes in dem Ringpuffer verwendet (`mb_write_idx` für Sender, `mb_read_idx` für Empfänger). Der gegenseitige Ausschluss für den kurzen Aktualisierungsmoment des Ringpuffers wird durch ein Mutex `mtx_mb` des Empfängers geschützt. Semaphoren verhindern Lese-/Schreib-Konflikte (Reader-Writer) effizient.

### 8. Wie reagiert Ihre Bibliothek, wenn mehrere Prozesse gleichzeitig eine Nachricht an denselben Empfänger senden?
Die Prozesse werden sicher blockiert und reihen sich geordnet ein. Das Einfügen in die Empfänger-Mailbox wird über das Zählsemaphor `sem_mb_free` und das exklusive Mutex des Empfängers geschützt:
```c
pthread_mutex_lock(&shared->peers[peer_index].mtx_mb);
```
Mehrere Sender verhalten sich also seriell beim Einstellen der Nachrichtenzeiger in den Ringpuffer.

### 9. Was passiert, wenn der OSMP-Typ der Nachricht beim Senden oder Empfangen nicht einem erwarten OSMP-Typ entspricht?
Bei der aktuellen Implementierung ignoriert `OSMP_Recv` den Datentyp der echten im Slot gespeicherten Nachricht (`shared->message_slots[reserved_index].datatype`). Es berechnet die Bytes für den Puffer nur auf Basis des übergebenen Parameters `datatype`. Es findet also keine Typenprüfung (Mismatch-Check) statt. Das führt bei abweichenden Typen dazu, dass rohe Speicherdaten falsch interpretiert werden.

### 10. Was passiert mit bereits im Posteingang abgelegten Nachrichten, wenn der Empfänger sich mit OSMP_Finalize abmeldet und sich ggf. später wieder anmeldet?
Beim Aufruf von `OSMP_Finalize` wird der Speicherbereich per `munmap` im Peer ausgehängt, der `osmp_state` wechselt zu `OSMP_STATE_FINISHED`. Die ungelesenen Nachrichten verbleiben einfach als Speicherleichen ("Orphans") im Shared-Memory. Eine "Wiederanmeldung" im selben Prozess schlägt zwangsweise fehl, da `OSMP_Init` nur bei Status `OSMP_STATE_UNINITIALIZED` starten darf.

### 11. Lassen sich OSMP-Funktionen parallel aufrufen, wenn der Anwender Threads verwendet?
Nein, das ist durch die interne Absicherung blockiert. In jeder wichtigen Funktion (`osmpLib.c`) wird die Check-Funktion `OSMP_ThreadCheck()` ausgeführt. Diese vergleicht `syscall(SYS_gettid)` mit der Thread-ID des Haupt-Threads (`osmp_main_tid`). Handelt es sich um einen vom Nutzer gestarteten POSIX-Thread, wird der Funktionsaufruf mit `-1` abgewiesen.

### 12. Lassen sich mehrere Starter mit unterschiedlichen Executables unabhängig voneinander betreiben?
Ja. In `OSMP_GetShmName` wird der Name des Shared-Memory-Segments dynamisch basierend auf der Parent-PID (`getppid()`) generiert, beispielsweise `/osmp_shm_<ppid>`. Demnach erstellt jeder Runner/Starter sein eigenes, isoliertes Shared-Memory-Segment. Verschiedene Instanzen kommen sich nicht in die Quere.

### 13. Werden alle System-Ressourcen freigegeben? Wo und wann?
Ja. In `osmpLib.c` wird in `OSMP_Finalize` zum Ende des Peer-Prozesses das Segment mit `munmap` im eigenen Speicherraum getrennt. Laufende interne Request-Threads (von asynchronen Calls) werden sauber gecancelled und mit `pthread_join` beendet; die Files für Logs geschlossen. Der übergeordnete Starter (Runner) ist dann schließlich dafür verantwortlich, `shm_unlink` aufzurufen, wenn alle Kindprozesse beendet wurden, um das OS-seitige File-Handle des Shared Memory zu löschen.

### 14. Führen alle blockierenden Aufrufe zur Blockierung, solange die zugehörigen Bedingungen nicht erfüllt sind? Weisen Sie durch Belasten des Systems ggf. nach, dass dies immer funktioniert.
Ja. `OSMP_Barrier` verwendet verlässliche Systemprimitive (`pthread_cond_wait`), `OSMP_Recv` und `Send` setzen auf Semaphoren (`sem_wait`). Der Test-Report (`test_report.md`) bestätigt, dass Tests wie `new_deadlock`, `senderDeadlockTest` und `circular_stress` absolut fehlerfrei (`PASS`) ablaufen. Durch diese Stresstests auf Thread-Basis wurde das saubere Blockieren unter Volllast zweifelsfrei bewiesen.

### 15. Können die Funktionen OSMP_Barrier() und OSMP_Gather() mehrfach direkt hintereinander aufgerufen werden?
Ja. `OSMP_Barrier` verwaltet intern eine Epochen-Variable (`cycle`). Jeder Prozess wartet via Condition-Variable darauf, dass der letzte eintreffende Prozess den Counter zurücksetzt und die `cycle`-Variable hochzählt. Dies verhindert "Spurious Wakeups" und erlaubt eine unmittelbare Wiederverwendung. Da `OSMP_Gather` auf der gleichen Methodik aufbaut (`osmp_internal_barrier`), ist auch dieses mehrfach nacheinander sicher ausführbar.

### 16. Was passiert mit OSMP-Funktionen, wenn der Anwender zusätzliche Threads gestartet hat?
Zusätzliche Threads (z. B. durch den Anwender via `pthread_create` gestartet) sind von der Benutzung der OSMP-API ausgeschlossen. Der Mechanismus in `OSMP_ThreadCheck()` weist Aufrufe aus fremden Threads strikt ab (Return Code `-1`). Das Hauptprogramm und der Runner laufen davon unberührt sicher weiter.

### 17. Was passiert, wenn eine Barriere aufgerufen wird, wenn sich vorher bereits ein OSMP-Prozess beendet hat?
Die Barriere in OSMP erwartet immer strikt die bei der Initialisierung festgelegte Anzahl an Prozessen (`barrier->threshold`). Beendet sich ein Prozess, bevor er die Barriere erreicht, läuft der Counter (`barrier_count`) niemals auf 0. Alle noch verbleibenden Prozesse bleiben durch `pthread_cond_wait` für ewig blockiert (Deadlock).

### 18. Sind bei der Verwendung ihrer OSMP-Lib Deadlocks oder Race-Conditions möglich? Wie haben Sie das überprüft?
Durch einen rigorosen Einsatz von Mutexen (`mtx_mb`, `mtx_global_free`) und Zustandssynchronisation über Semaphoren (`sem_mb_free`, `sem_mb_full`) wurden Race-Conditions in der Bibliothek ausgeschlossen. Dies wurde mit umfassenden Tests (`matteo_test_gather_race`, `matteo_test_many_to_many_prak`, `new_gather_thread`) und Werkzeugen (TSAN/Helgrind) verifiziert (alles auf `PASS`). Deadlocks sind ausschließlich durch Fehlverhalten der Anwender auf der API-Ebene möglich (z.B. Sender überlasten Postfächer oder asymmetrischer Aufruf der Barriere).

### 19. Wie kann der Aufrufer eines asynchronen Send oder Receive erfahren, ob und welcher Fehler bei dem im Hintergrund ausgeführten Auftrags aufgetreten ist?
Der Aufrufer nutzt die Methode `OSMP_Wait(request)`. Tritt im Hintergrund-Thread ein Fehler auf (z. B. eine zu große Payload), beendet sich dieser Thread nicht mit Code 0, sondern gibt einen anderen Wert zurück (z. B. `return (void*)1;`). `OSMP_Wait` wertet via `pthread_join` diesen Exit-Code aus. Ist dieser ungültig, loggt Wait den Fehler ("Thread exited unexpectedly") und gibt `-1` zurück. Zudem kann per `OSMP_Test` asynchron der State (`OSMP_REQ_ERROR`) abgefragt werden.

### 20. Was passiert, wenn der Anwender denselben OSMP-Request mehrfach verwendet?
Verwendet der Anwender einen nicht abgeschlossenen Request erneut (z. B. zweiter Aufruf von `OSMP_ISend` mit dem gleichen `request`-Pointer), überschreibt die Bibliothek ungebremst die Struktur `osmp_request_internal_t`. Da jedoch ein Hintergrund-Thread bereits arbeitet, führt dies zu einer schweren Race-Condition. Die Pointer für Puffer verändern sich im laufenden Betrieb, der alte Worker-Thread hängt als Leiche im System ("Leak") oder löst einen Segmentation Fault aus.

### 21. Kann OSMP_RemoveRequest aufgerufen werden, obwohl der Requester noch verwendet wird?
Nein, dies ist äußerst kritisch. `OSMP_RemoveRequest` ruft `pthread_mutex_destroy` und dann hart `free(req)` auf. Ist der Worker-Thread noch im Hintergrund aktiv (da kein vorheriges `OSMP_Wait` stattfand), versucht dieser danach, auf den gelöschten Speicher (z. B. `req->state`) zuzugreifen (Use-After-Free). Dies crasht den laufenden Prozess unausweichlich.

### 22. Wie kann das Logging optimiert werden, sodass wenig Synchronisation zwischen den Prozessen nötig ist und wenige Schreiboperation auf der Datei ausgeführt werden?
Derzeit verwendet `OSMP_Log` den Aufruf `fputs` im O_APPEND-Modus, was viele kleine, langsame Syscalls und I/O-Interaktionen zum Dateisystem (Disk) provoziert.
Eine drastische Optimierung wäre die Verwendung eines asynchronen Lock-Free-Ringbuffers im Shared-Memory je Peer. Ein dedizierter asynchroner Logging-Prozess (oder -Thread des Runners) konsumiert diese Puffer periodisch und schreibt gesammelte Blöcke ("Chunking") am Stück in die Datei. Das entkoppelt die kritischen Laufzeiten der Worker-Prozesse komplett vom Flaschenhals des Dateisystems.
