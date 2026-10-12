# Ergebnisdokumentation der Tests

Diese Dokumentation fasst die Ergebnisse der Testphase für den OSMP-Runner und das Echo-Tool zusammen und dient als Checkliste für den ersten Meilenstein.

## 1. Projekt-Checkliste (Termin 1)

### Projektgrundlage und Organisation
- [x] Projekt-Template korrekt übernommen und in Repository eingepflegt.
- [x] Repository korrekt eingerichtet (Branch main, Struktur, keine Fremdanteile).
- [x] Relevante Artefakte (Code, Dokumentation, Logs etc.) vollständig im Repository enthalten.

### Dokumentation und Präsentation
- [x] Quelltexte und Ergebnisse (z. B. Ablauf, Testergebnisse) angemessen dokumentiert.
- [x] README.md enthält technische und organisatorische Informationen.
- [x] Ablaufskizze oder Diagramm zur Steuerung des Prozesses vorhanden (siehe `Prozesssteuerung.md`).

### Systemaufrufe und Betriebssystembezug
- [x] Verständnis der theoretischen Grundlagen (siehe [Abschnitt 2](#2-theoretischer-hintergrund-diskussion)).
- [x] Systemaufrufe fork, exec*, wait, waitpid korrekt verwendet (in `osmpRun.c`).

### Technische Umsetzung und Funktion
- [x] Projekt lässt sich fehlerfrei kompilieren (aktivierte Warnungen und `-fsanitize` in `CMakeLists.txt`).
- [x] Programm führt erwartungsgemäß aus (Argumente, Rückgabewerte).
- [x] Die Anzahl der zu startenden Prozesse kann frei angegeben werden, Fehler werden erkannt.
- [x] Peer-Verwaltung im Runner über eine geeignete Struktur realisiert (`osmp_peer_t`).
- [x] Testfälle für Grundfunktionalität und Fehlerfälle vorhanden (siehe `run_tests.sh`).
- [x] Relevante Ereignisse (Start, Fehler, Ende) geloggt oder dokumentiert.
- [x] Der Runner gibt am Schluss alle noch belegten Ressourcen wieder frei (`free`, `fclose`).

### Reflexion und Ausblick
- [x] Offene Fragen oder Probleme aus dem Termin dokumentiert (siehe `Todo.md`).
- [x] Nächste Schritte im Team oder Repository geplant.
- [x] Erste Auseinandersetzung mit Shared Memory (Termin 2) erfolgt.
- [x] Verständnis der Funktionen `OSMP_Init()`, `OSMP_Size()`, `OSMP_Rank()` etc.
- [x] Erste Ideen zur Umsetzung des nächsten Termins vorhanden.

## 2. Theoretischer Hintergrund (Diskussion)

### Was versteht man unter einem Prozess im Betriebssystem?
Ein Prozess ist ein **Programm in Ausführung**. Er umfasst den Programmcode, den aktuellen Zustand (CPU-Register, Program Counter), den Stack (lokale Variablen, Funktionsaufrufe), den Heap (dynamischer Speicher) und den Datenbereich (globale Variablen). Jeder Prozess besitzt einen Prozesskontrollblock (PCB) im Kernel.

### Welche typischen Zustände kann ein Prozess haben?
- **New:** Prozess wird gerade erstellt.
- **Ready:** Prozess ist bereit zum Rechnen und wartet auf den Scheduler.
- **Running:** Prozess wird aktuell von der CPU ausgeführt.
- **Waiting/Blocked:** Prozess wartet auf ein Ereignis (z. B. I/O).
- **Terminated:** Ausführung beendet. (Ein "Zombie" ist ein beendeter Prozess, dessen Exit-Status noch nicht vom Elternprozess abgefragt wurde).

### Was passiert genau, wenn ein Prozess durch fork() erzeugt wird?
`fork()` erzeugt ein Duplikat des aufrufenden Prozesses (Elternprozess). Der neue Prozess (Kindprozess) erhält eine neue PID. Der gesamte Adressraum wird kopiert (technisch oft via *Copy-on-Write*), ebenso wie Dateideskriptoren und Umgebungsvariablen. `fork()` gibt im Elternprozess die PID des Kindes zurück, im Kindprozess den Wert 0.

### Wie unterscheiden sich Eltern- und Kindprozesse in Bezug auf Adressraum und Ressourcen?
Sie haben **getrennte Adressräume**. Änderungen an Variablen in einem Prozess beeinflussen den anderen nicht. Der Kindprozess erbt jedoch Kopien der Ressourcen (z. B. offene Dateien). Er besitzt eine eigene PID und die PPID (Parent PID) zeigt auf den Elternprozess.

### Was bewirken die verschiedenen exec()-Varianten und warum ist ihre Auswahl wichtig?
Die `exec()`-Familie ersetzt das aktuelle Speicherbild durch ein neues Programm. Die Auswahl ist wichtig für:
- **Argumentübergabe:** `l` (Liste) vs. `v` (Vektor/Array).
- **Suchpfad:** `p` (nutzt PATH-Umgebungsvariable) vs. absoluter Pfad.
- **Umgebung:** `e` erlaubt das explizite Setzen von Umgebungsvariablen.
Im Runner wird `execvp` genutzt, da es den Suchpfad verwendet und ein Argumenten-Array (`argv`) akzeptiert.

## 3. Exemplarische Log-Auszüge

Ein typischer Lauf mit 2 Prozessen erzeugt folgende Log-Einträge:

```text
2026-04-15T18:53:07 1 226334 [RUNNER] Starte 2 Peers mit Executable './build/echoall'.
2026-04-15T18:53:17 1 226334 [RUNNER] sucessfully terminated child process 0 (PID: 226335) 
2026-04-15T18:53:17 1 226334 [RUNNER] sucessfully terminated child process 1 (PID: 226336) 
```

**Analyse:**
- Der Zeitstempel zeigt die korrekte Laufzeit (ca. 10 Sekunden, passend zum `sleep(1)` im `echoall`).
- Jeder Prozess wird mit seiner korrekten PID und dem Index im Peer-Array geloggt.
- Das Loglevel (hier 1) wird korrekt mitgeschrieben.

## 2. Kurz-Analyse der Testergebnisse

| Testfall | Ergebnis | Anmerkung |
| :--- | :--- | :--- |
| Standard-Start | **Bestanden** | Prozesse starten und beenden sich sauber. |
| Große Prozessanzahl | **Bestanden** | 500 Prozesse wurden erfolgreich verwaltet (Ressourcenlimit abhängig). |
| Ungültige Parameter | **Bestanden** | `-p 0` und fehlendes `-e` werden korrekt abgefangen. |
| Logging | **Bestanden** | Dateien werden nur erstellt, wenn Flags gesetzt sind; Default-Level 1 funktioniert. |
| Fehlerhafte Executables | **Teilweise** | Fehlermeldung wird ausgegeben, aber der Runner gibt momentan immer Exit-Code 0 zurück. |

### Gefundene Fehler / Beobachtungen
- **Exit-Status:** Der `osmprun` liefert auch dann Erfolg zurück, wenn das Ziel-Executable nicht gefunden wurde. Das liegt daran, dass der Parent-Prozess nur prüft, ob der Kindprozess beendet wurde, aber nicht dessen Exit-Status (`WEXITSTATUS`) auswertet.
- Log-Format: In der Logdatei ist ein zusätzlicher Zeilenumbruch am Ende der "terminated"-Nachricht, was zu Leerzeilen führt.

