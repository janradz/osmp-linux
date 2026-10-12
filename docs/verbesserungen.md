# OSMP Refactoring und Verbesserungen: Technische Dokumentation

Dieses Dokument beschreibt im Detail die gefundenen und behobenen Schwachstellen der OSMP-Bibliothek (Stand: Refactoring-Phase). Ziel dieser Dokumentation ist es, die technischen Ursachen der ursprünglichen Probleme (wie Deadlocks, Segmentation Faults und Race Conditions) präzise für Code-Reviews aufzuschlüsseln und die implementierten Lösungen sowie deren Use-Cases darzulegen.

---

## 1. Deadlocks durch Architekturbrüche bei asynchronen Aufrufen

### Das Problem
Die Bibliothek bot mit `OSMP_ISend` und `OSMP_IRecv` asynchrone Funktionen an. Die aufgerufenen Hintergrund-Threads (`thread_send` und `thread_recv`) riefen jedoch intern stumpf die synchronen Funktionen `OSMP_Send` und `OSMP_Recv` auf. Innerhalb dieser synchronen Funktionen befand sich der `OSMP_ThreadCheck()`, welcher prüft, ob die Funktion vom "Main"-Thread des Prozesses aufgerufen wurde.
Dies führte dazu, dass der `OSMP_ThreadCheck` bei asynchroner Ausführung korrekterweise erkannte: "Dies ist ein Hintergrund-Thread!". Er brach die Sende-/Empfangsoperation sofort ab und der Request blieb in einem ewigen Limbo hängen, was letztendlich in `OSMP_Wait` zum Deadlock führte.

### Die Lösung
Die Architektur wurde umgedreht (**Wrapper-Pattern**):
Die eigentliche Kernlogik (Reservieren des Shared Memory, Mutexe, Semaphore, `memcpy`) wurde zu 100 % in die asynchronen Worker-Threads (`thread_send` / `thread_recv`) verlagert. 
Die synchronen Funktionen (`OSMP_Send` / `OSMP_Recv`) fungieren nun nur noch als Wrapper. Sie führen den `OSMP_ThreadCheck()` aus (Sicherheit für die Public API) und rufen dann `OSMP_ISend` gefolgt von `OSMP_Wait` auf.

### Konkreter Use-Case (Wann crashte es?)
*Ein Peer versucht, 5 Nachrichten asynchron zu versenden, um während der Wartezeit Berechnungen durchzuführen.*
**Vorher:** Der Hintergrund-Thread brach ab. Der Nutzer rief irgendwann `OSMP_Wait` auf und die Applikation fror für immer ein (Deadlock).
**Nachher:** Die asynchrone Logik arbeitet ungestört vom Hauptthread.

---

## 2. Ungesicherte Kritische Abschnitte (Data Races auf dem Request-State)

### Das Problem
Der Status eines Requests (`req->state`) wurde vom Hintergrund-Thread (beim Abschluss auf `OSMP_REQ_COMPLETE`) überschrieben und zeitgleich vom Main-Thread in `OSMP_Test` (für Poll-Schleifen) oder `OSMP_Wait` ausgelesen. Dies geschah ohne jegliche Synchronisation (kein Mutex, kein Atomic). Laut C11-Standard ist dies eine Data Race und erzeugt in Sanitizern starke Warnungen.

### Die Lösung
Die interne `osmp_request_internal_t`-Struktur wurde um einen eigenen `pthread_mutex_t mtx_state` erweitert. Dieser wird in `OSMP_CreateRequest` initialisiert. Jegliches Setzen (`set_req_state()`) und Auslesen (in `OSMP_Test`) ist jetzt durch diesen Mutex gesichert.

### Konkreter Use-Case (Wann crashte es?)
*Ein Entwickler schreibt eine enge `while`-Schleife um `OSMP_Test`, um auf das Ende eines Requests zu warten.*
Auf manchen Architekturen (z. B. schwach geordneten Speicherarchitekturen wie ARM) konnte es passieren, dass der Main-Thread niemals den vom Worker-Thread geschriebenen Status "sieht", weil Caching-Effekte nicht synchronisiert wurden. Das Warten wäre zur Endlosschleife geworden.

---

## 3. Speicherzugriffsfehler beim Beenden (`OSMP_Finalize`)

### Das Problem
Wurde `OSMP_Finalize` aufgerufen, entfernte die Bibliothek sofort das globale Shared Memory per `munmap()`. Hintergrund-Threads, die noch aktiv Daten in die Mailbox schreiben oder daraus lesen wollten, griffen plötzlich ins Nichts (Segfault!). Es gab keine Logik, die die Worker-Threads abfing.

### Die Lösung
In `OSMP_Finalize` wurde eine Thread-Handle-Logik eingebaut: Es wird durch alle intern registrierten Requests iteriert. Sind diese noch aktiv, werden die Threads via `pthread_cancel` (an sicheren Abbruchpunkten wie den Semaphoren) abgebrochen und via `pthread_join` eingesammelt.

### Konkreter Use-Case (Wann crashte es?)
*Ein User versendet asynchron eine Nachricht, bekommt aber einen unerwarteten Fehler und ruft vorzeitig `OSMP_Finalize` und `exit()` auf.*
Der laufende Sende-Thread versuchte parallel noch den Mutex auf dem Shared Memory zu holen und verursachte einen harten Crash beim Beenden der Anwendung.

---

## 4. Speicherlecks und Dangling Pointers

### Das Problem
1. **Lebensdauer von Variablen (`asyncTest.c`)**: Variablen, die an asynchrone Sende-Puffer übergeben wurden, waren lokal innerhalb einer For-Schleife deklariert. Der Zeiger verfiel beim nächsten Schleifendurchlauf, bevor der Hintergrund-Thread ihn lesen konnte.
2. **Speicherleck in `OSMP_RemoveRequest`**: Bei der Entfernung von Requests wurde zwar ein `free()` auf den Speicher gemacht, der Pointer blieb jedoch dauerhaft im internen Tracking-Array `requests[256]` liegen ("Dangling Pointer"). 

### Die Lösung
Das lokale Test-Array wurde in den äußeren Scope gehoben, sodass der Speicher stabil bleibt, bis die asynchronen Aktionen abgeschlossen sind.
In `OSMP_RemoveRequest` wird der freigegebene Zeiger nun aktiv in dem internen Tracking-Array gesucht und auf `NULL` gesetzt.

### Konkreter Use-Case (Wann crashte es?)
*Senden von Arrays in einer Schleife.*
Der Empfänger bekam oft willkürlichen Müll (Zahlenmatsch) geschickt, weil der asynchrone Hintergrund-Thread auf einen Stack-Bereich zugriff, der vom Main-Thread bereits für eine neue Iteration mit fremden Daten überschrieben wurde.

---

## 5. Pedantische Fallstricke & C-Spezifika

Einige Logikfehler hätten in strengen Prüfungen durch automatische Scanner oder bei einem strengen Code-Review Punktabzug bedeutet:
- **Ignorierte Rückgabewerte**: In `OSMP_ISend` wurde der Rückgabewert von `pthread_create` ignoriert. Selbst wenn das Betriebssystem den Thread mangels Ressourcen nicht erzeugen konnte, lieferte die Methode `OSMP_SUCCESS` zurück. Dies wurde behoben (liefert nun `-1`).
- **Logikfehler beim Längenvergleich**: In `thread_recv` wurde geloggt: *"Requested datacount smaller than available data"*, selbst wenn die Längen exakt übereinstimmten. Die Bedingungen (If/Else-If/Else) wurden korrekt nachgestellt.
- **Täuschende UNUSED-Makros**: Parameter wie `count`, die im weiteren Verlauf aktiv zur Größenberechnung genutzt wurden, trugen das `#define UNUSED` Makro. Dies wurde entfernt, um Code-Confusion zu vermeiden.

---

## Neue, "Hardcore" Test-Szenarien

Zum Nachweis der Robustheit wurden in `src/osmpExecutables/` drei Extremtests hinzugefügt, die alle der C-Bibliothek zur Verfügung stehenden Limits bis zum Äußersten reizen:

1. `circular_stress.c`: Produziert einen absichtlichen Ring aus Nachrichten, der ohne funktionierende asynchrone Kommunikation garantiert blockiert. 
2. `storm_test.c`: Testet die Semaphoren durch parallelen "Beschuss" einer einzigen Empfänger-Mailbox durch hunderte Nachrichten gleichzeitig.
3. `leak_test.c`: Stresstestet die dynamische Speicherallokation (`malloc`/`free`) für Requests in einer engen Schleife.
