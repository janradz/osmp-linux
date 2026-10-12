# Anleitung für manuelle Tests und Beobachtungen

Diese Anleitung beschreibt, wie spezielle Szenarien manuell geprüft werden können, die über automatisierte Tests hinausgehen.

## 1. Verhalten bei Syscall-Fehlern (Speicherknappheit)
Um zu prüfen, wie der `osmprun` reagiert, wenn `malloc` fehlschlägt oder Ressourcen erschöpft sind:
- **Test:** Versuche eine extrem hohe Anzahl an Prozessen zu starten, die das Systemlimit übersteigt (z. B. 1.000.000).
- **Befehl:** `./build/osmprun -p 1000000 -e ./build/echoall test`
- **Beobachtung:** Prüfe, ob das Programm kontrolliert mit einer Fehlermeldung ("Not enough memory" oder "fork()") abbricht und nicht abstürzt (Segfault).

## 2. Verhalten bei vorzeitigem Abbruch des Elternprozesses
Hier wird geprüft, was passiert, wenn der Runner (Parent) getötet wird, während die Kindprozesse noch laufen.
- **Vorbereitung:** Nutze `echoall`, da dieses 10 Sekunden lang schläft.
- **Befehl 1:** `./build/osmprun -p 4 -e ./build/echoall test &`
- **Befehl 2 (in neuem Terminal oder sofort danach):** `pkill osmprun`
- **Beobachtung:** Prüfe mit `ps -ef | grep echoall`, ob die Kinder weiterhin laufen (sie sollten nun den `init`-Prozess (PID 1) als neuen Parent haben).

## 3. Prüfung auf Zombie-Prozesse
Nachdem ein OSMP-Lauf beendet wurde, dürfen keine Prozessleichen zurückbleiben.
- **Test:** Führe einen normalen Lauf aus und prüfe sofort danach die Prozesstabelle.
- **Befehl:** `./build/osmprun -p 4 -e ./build/echoall test && ps aux | grep Z`
- **Beobachtung:** Wenn außer der `grep`-Zeile nichts angezeigt wird, wurden alle Prozesse korrekt mittels `waitpid` eingesammelt.

## 4. Konsistenz der Logs
Prüfung, ob alle Ereignisse (Start, Ende) für jeden Prozess geloggt wurden.
- **Befehl:** `./build/osmprun -p 5 -l manual.log -e ./build/echoall test`
- **Analyse:**
    - Zähle die Zeilen: Bei 5 Prozessen sollten mindestens 1 Start-Eintrag (Runner) und 5 Beendigungs-Einträge vorhanden sein.
    - Befehl: `grep "successfully terminated" manual.log | wc -l` (Sollte 5 ergeben).

## 5. OSMP Funktionen
### Rank system
```bash
./osmprun -p 4 -v 3 -e ./echoall test
```
Terminal:
```
Building 4 Processes of Executable ./echoall
size of shared memory struct is: 2960 
shm creation pointer 0x7f63ee31e000
creation size 2960
Argument 1: test
[PID: 23273 | PPID: 23271 | TID: 23273 | Rank 1]
Argument 1: test
[PID: 23274 | PPID: 23271 | TID: 23274 | Rank 2]
Argument 1: test
[PID: 23272 | PPID: 23271 | TID: 23272 | Rank 0]
Argument 1: test
[PID: 23275 | PPID: 23271 | TID: 23275 | Rank 3]
[PID: 23272 | PPID: 23271 | TID: 23272 | Rank 0]
[PID: 23273 | PPID: 23271 | TID: 23273 | Rank 1]
[PID: 23274 | PPID: 23271 | TID: 23274 | Rank 2]
[PID: 23275 | PPID: 23271 | TID: 23275 | Rank 3]
[PID: 23275 | PPID: 23271 | TID: 23275 | Rank 3]
[PID: 23272 | PPID: 23271 | TID: 23272 | Rank 0]
[PID: 23274 | PPID: 23271 | TID: 23274 | Rank 2]
[PID: 23273 | PPID: 23271 | TID: 23273 | Rank 1]
```
Logfile:
```
2026-04-29T13:59:47 1 22275 [RUNNER] Starte 4 Peers mit Executable './echoall'.
2026-04-29T13:59:50 1 22275 [RUNNER] child process 0 (PID: 22276) terminated with exit code 0
2026-04-29T13:59:50 1 22275 [RUNNER] child process 1 (PID: 22277) terminated with exit code 0
2026-04-29T13:59:50 1 22275 [RUNNER] child process 2 (PID: 22278) terminated with exit code 0
2026-04-29T13:59:50 1 22275 [RUNNER] child process 3 (PID: 22279) terminated with exit code 0
2026-04-29T13:59:50 1 22275 [RUNNER]: Started Shared Memory cleanup
2026-04-29T13:59:50 1 22275 [INIT] Initialized OSMP system

2026-04-29T13:59:50 1 22275 [INIT] Initialized OSMP system

2026-04-29T13:59:50 1 22275 [INIT] Initialized OSMP system

2026-04-29T13:59:50 1 22275 [INIT] Initialized OSMP system

2026-04-29T13:59:50 1 22275 [RANK]: Returned Rank: 1 for Process 22277

2026-04-29T13:59:50 1 22275 [RANK]: Returned Rank: 2 for Process 22278

2026-04-29T13:59:50 1 22275 [RANK]: Returned Rank: 0 for Process 22276

2026-04-29T13:59:50 1 22275 [RANK]: Returned Rank: 3 for Process 22279

2026-04-29T14:14:20 1 23271 [RUNNER] Starte 4 Peers mit Executable './echoall'.
2026-04-29T14:14:23 1 23271 [RUNNER] child process 0 (PID: 23272) terminated with exit code 0
2026-04-29T14:14:23 1 23271 [RUNNER] child process 1 (PID: 23273) terminated with exit code 0
2026-04-29T14:14:23 1 23271 [RUNNER] child process 2 (PID: 23274) terminated with exit code 0
2026-04-29T14:14:23 1 23271 [RUNNER] child process 3 (PID: 23275) terminated with exit code 0
2026-04-29T14:14:23 1 23271 [RUNNER]: Started Shared Memory cleanup
2026-04-29T14:14:23 1 23271 [INIT] Initialized OSMP system

2026-04-29T14:14:23 1 23271 [INIT] Initialized OSMP system

2026-04-29T14:14:23 1 23271 [INIT] Initialized OSMP system

2026-04-29T14:14:23 1 23271 [RANK]: Returned Rank: 1 for Process 23273

2026-04-29T14:14:23 1 23271 [RANK]: Returned Rank: 2 for Process 23274

2026-04-29T14:14:23 1 23271 [RANK]: Returned Rank: 0 for Process 23272

2026-04-29T14:14:23 1 23271 [INIT] Initialized OSMP system

2026-04-29T14:14:23 1 23271 [RANK]: Returned Rank: 3 for Process 23275
```

### Logger Executable
```bash
./osmprun -p 1 -v 3 -e ./logger
```
Logfile:
```
2026-04-29T14:17:01 1 23445 [RUNNER] Starte 1 Peers mit Executable './logger'.
2026-04-29T14:17:01 1 23445 [RUNNER] child process 0 (PID: 23446) terminated with exit code 0
2026-04-29T14:17:01 1 23445 [RUNNER]: Started Shared Memory cleanup
2026-04-29T14:17:01 1 23445 [INIT] Initialized OSMP system

2026-04-29T14:17:01 1 23445 [Logger] A level one logging Message
2026-04-29T14:17:01 2 23445 [Logger] A level two logging Message
2026-04-29T14:17:01 3 23445 [Logger] A level 3 logging Message
```

## 6. Asynchrone und Synchronisations-Tests (Neu)
Mit der Implementierung der asynchronen Kommunikation und den neuen Synchronisations-Primitiven wurden weitere Test-Szenarien hinzugefügt, um die Stabilität bei hoher Last und Parallelität zu gewährleisten.

### 6.1 Barrier Test (`test_barrier`)
Dieser Test überprüft die korrekte Funktionsweise der `OSMP_Barrier`. Alle Prozesse müssen den Barrier-Punkt erreichen, bevor sie fortfahren dürfen.
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/test_barrier`
- **Beobachtung:** Die Logausgaben zeigen, dass alle Prozesse initialisiert werden und erst nach dem gemeinsamen Erreichen der Barriere geordnet terminieren.

**Logauszug:**
```
2026-06-25T10:18:56 1 65269 [RUNNER] Starte 4 Peers mit Executable './test_barrier'.
2026-06-25T10:18:56804.477752 1 65272 [INIT] Process 65272 successfully initialized with Rank 2
2026-06-25T10:18:56802.129193 1 65271 [INIT] Process 65271 successfully initialized with Rank 1
2026-06-25T10:18:56802.178906 1 65270 [INIT] Process 65270 successfully initialized with Rank 0
2026-06-25T10:18:56805.247480 1 65273 [INIT] Process 65273 successfully initialized with Rank 3
2026-06-25T10:18:56839.789400 1 65273 [FINALIZE] Peer with Rank 3 terminated
2026-06-25T10:18:56 1 65269 [RUNNER] child process 0 (PID: 65270) terminated with exit code 0
2026-06-25T10:18:56 1 65269 [RUNNER] child process 1 (PID: 65271) terminated with exit code 0
2026-06-25T10:18:56 1 65269 [RUNNER]: Started Shared Memory cleanup
```

### 6.2 Async Test (`asyncTest`)
Dieser Test validiert die asynchrone Nachrichtenübermittlung mittels `OSMP_ISend` und `OSMP_IRecv`.
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/asyncTest`
- **Beobachtung:** Es wird geprüft, ob asynchrone Requests korrekt in die Queue (`Added new Request`) eingefügt und vom Background-Worker abgearbeitet werden.

**Logauszug:**
```
2026-06-25T10:18:56 1 65279 [RUNNER] Starte 4 Peers mit Executable './asyncTest'.
2026-06-25T10:18:56883.591392 1 65282 [INIT] Process 65282 successfully initialized with Rank 2
2026-06-25T10:18:56885.562972 3 65282 [SR_PEER]: Now sending message to all peers
2026-06-25T10:18:56885.793966 3 65282 [OSMP ISend]: Added new Request
2026-06-25T10:18:56886.102547 3 65282 [OSMP ISend]: Added new Request
2026-06-25T10:18:56886.449029 3 65282 [SR_PEER]: Now Reading messages
2026-06-25T10:18:56888.670729 1 65283 [FINALIZE] Peer with Rank 3 terminated
2026-06-25T10:18:56 1 65279 [RUNNER] child process 0 (PID: 65280) terminated with exit code 0
2026-06-25T10:18:56 1 65279 [RUNNER]: Started Shared Memory cleanup
```

### 6.3 Storm Test (`storm_test`)
Ein Stresstest für das System, bei dem extrem viele Nachrichten in kürzester Zeit versendet werden, um Race Conditions und Queue-Überläufe zu testen.
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/storm_test`
- **Beobachtung:** Die Requests werden in sehr hoher Frequenz in die Queue gepusht (`Added new Request`), das System bewältigt diese Last jedoch fehlerfrei, bis alle Prozesse regulär terminieren.

**Logauszug:**
```
2026-06-25T10:18:56 1 65313 [RUNNER] Starte 4 Peers mit Executable './storm_test'.
2026-06-25T10:18:56932.969566 1 65314 [INIT] Process 65314 successfully initialized with Rank 0
2026-06-25T10:18:56933.616804 3 65314 [OSMP ISend]: Added new Request
2026-06-25T10:18:56934.859312 3 65314 [OSMP ISend]: Added new Request
2026-06-25T10:18:56935.189673 3 65314 [OSMP ISend]: Added new Request
...
2026-06-25T10:18:56998.044684 1 65316 [FINALIZE] Peer with Rank 2 terminated
2026-06-25T10:18:57 1 65313 [RUNNER] child process 0 (PID: 65314) terminated with exit code 0
2026-06-25T10:18:57 1 65313 [RUNNER]: Started Shared Memory cleanup
```

### 6.4 Deadlock Test (`senderdeadlocktest`)
Dieser Test erzeugt absichtlich Kommunikationsmuster, die ohne eine saubere asynchrone / synchronisationsgesicherte Verarbeitung zu einem Deadlock führen würden.
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/senderdeadlocktest`
- **Beobachtung:** Die Prozesse bleiben nicht hängen, sondern verarbeiten die Nachrichten erfolgreich (hier sichtbar durch `OSMP_Recv` truncating/receiving Meldungen) und terminieren ohne Deadlocks.

**Logauszug:**
```
2026-06-25T10:18:57 1 65623 [RUNNER] Starte 4 Peers mit Executable './senderdeadlocktest'.
2026-06-25T10:18:5751.735532 1 65625 [INIT] Process 65625 successfully initialized with Rank 1
2026-06-25T10:18:5752.283312 3 65625 [SR_PEER]: Now sending message to all peers
2026-06-25T10:18:5752.488479 3 65625 [OSMP ISend]: Added new Request
2026-06-25T10:18:5754.115740 3 65625 [SR_PEER]: Now Reading messages
2026-06-25T10:18:5754.378044 3 65625 [OSMP_Recv] Requested datacount larger than available data, truncating
2026-06-25T10:18:5755.839964 1 65627 [FINALIZE] Peer with Rank 3 terminated
2026-06-25T10:18:57 1 65623 [RUNNER] child process 1 (PID: 65625) terminated with exit code 0
2026-06-25T10:18:57 1 65623 [RUNNER]: Started Shared Memory cleanup
```

### 6.5 Circular Stress Test (`circular_stress`)
Testet eine zyklische (kreisförmige) Abhängigkeit beim Senden und Empfangen von Nachrichten unter hoher Last zwischen den Prozessen.
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/circular_stress`
- **Beobachtung:** Jeder Prozess kommuniziert fortlaufend mit seinem zyklischen Nachbarn. Die asynchrone Queue-Struktur meistert diese Abhängigkeiten fehlerfrei und ohne Abstürze.

**Logauszug:**
```
2026-06-25T10:18:57 1 65657 [RUNNER] Starte 4 Peers mit Executable './circular_stress'.
2026-06-25T10:18:57101.036200 1 65659 [INIT] Process 65659 successfully initialized with Rank 1
2026-06-25T10:18:57101.977551 3 65659 [OSMP ISend]: Added new Request
2026-06-25T10:18:57102.298014 3 65659 [OSMP ISend]: Added new Request
2026-06-25T10:18:57102.490386 3 65659 [OSMP ISend]: Added new Request
...
2026-06-25T10:18:57162.295658 1 65661 [FINALIZE] Peer with Rank 3 terminated
2026-06-25T10:18:57 1 65657 [RUNNER] child process 0 (PID: 65658) terminated with exit code 0
2026-06-25T10:18:57 1 65657 [RUNNER]: Started Shared Memory cleanup
```

### 6.6 Leak Test (`leak_test`)
Dieser Test überprüft das System auf mögliche Memory Leaks, insbesondere bei der wiederholten Nutzung von asynchronen Requests und der Queue-Verwaltung. Das Projekt wird mit AddressSanitizer kompiliert.
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/leak_test`
- **Beobachtung:** Der Test läuft durch und AddressSanitizer meldet keine Fehler (keine "Memory Leak"-Warnungen), was bestätigt, dass allozierter Speicher für Requests, Queues und Threads am Ende der Lebensdauer (oder beim Prozessende) korrekt freigegeben wird.

**Logauszug:**
```
2026-06-25T10:21:05 1 66679 [RUNNER] Starte 4 Peers mit Executable './leak_test'.
2026-06-25T10:21:0622.864761 1 66680 [INIT] Process 66680 successfully initialized with Rank 0
...
2026-06-25T10:21:0627.452112 1 66680 [FINALIZE] Peer with Rank 0 terminated
2026-06-25T10:21:06 1 66679 [RUNNER] child process 0 (PID: 66680) terminated with exit code 0
2026-06-25T10:21:06 1 66679 [RUNNER]: Started Shared Memory cleanup
```

### 6.7 Crash Test (`crash_test`)
Prüft das Fehlerbehandlungs-Verhalten des `osmprun`, wenn ein Child-Prozess während der Ausführung unerwartet abstürzt (z. B. durch einen Segfault).
- **Befehl:** `./build/osmprun -p 4 -v 3 -e ./build/crash_test`
- **Beobachtung:** Der absichtlich provozierte Nullpointer-Zugriff führt zum sofortigen Abbruch der Peers durch den AddressSanitizer (`SEGV on unknown address`). Der Runner registriert den vorzeitigen Exit, meldet einen Fehler (`Failed to validate all child processes. Initiating Kill all.`) und räumt verbliebene Prozesse sowie den Shared Memory auf.

**Logauszug:**
```
2026-06-25T10:21:06 1 66689 [RUNNER] Starte 4 Peers mit Executable './crash_test'.
==66691==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
...
SUMMARY: AddressSanitizer: SEGV /home/jan/bs-template/src/osmpExecutables/crash_test.c:26 in main
[RUNNER] Fatal: Peer process 66690 died unexpectedly during init!
2026-06-25T10:21:09 3 66689 [RUNNER] Failed to validate all child processes. Initiating Kill all.
```