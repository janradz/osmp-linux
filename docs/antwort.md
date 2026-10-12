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
