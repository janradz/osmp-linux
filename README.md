# OSMP – Message Passing unter Linux

Eine in C implementierte Bibliothek zur Kommunikation zwischen Prozessen auf
einem Rechner. Ein Runner startet die Prozesse; Nachrichten werden über gemeinsam
genutzten Speicher und synchronisierte Mailboxen ausgetauscht.

**Hochschul-Teamprojekt · C11 · POSIX · Linux · Pthreads · CMake**

## Schwerpunkte

- Prozessstart mit `fork`/`exec` und zentraler Ressourcenverwaltung.
- Shared Memory, Semaphore, Mutexe und Mailbox-Ringpuffer.
- Blockierende und asynchrone Send-/Receive-Operationen.
- Kollektive Operationen wie Barrier und Gather.
- Beispielprogramme für Kommunikation, Synchronisation und Fehlerszenarien.

## Eigener Beitrag

Mitarbeit an der Synchronisation paralleler Prozesse und Behebung von Fehlern, die zu gegenseitigen Blockierungen führten.

## Build und Ausführung

Voraussetzungen: Linux, GCC, CMake ab 3.20.

```bash
cmake -S . -B build -DSANITIZER=none
cmake --build build
./build/osmprun -p 2 -e ./build/echoall hello
./build/osmprun -p 2 -e ./build/senderreceiver
```

Sanitizer lassen sich beispielsweise mit `-DSANITIZER=address,undefined`
aktivieren. Die Testprogramme `crash_test` und `senderdeadlocktest` provozieren
absichtlich Fehler beziehungsweise Blockierungen. Das ursprüngliche `run_tests.sh`
enthält einen Test mit 500 Prozessen und ist kein kleiner Smoke-Test.

## Aufbau

| Ordner | Inhalt |
|---|---|
| `src/osmpLibrary` | Öffentliche API und Implementierung |
| `src/osmpRunner` | Prozessverwaltung und gemeinsamer Speicher |
| `src/osmpExecutables` | Beispiele und Tests |
| `docs` | Technische Projektdokumentation |

Die ursprüngliche Projektdokumentation liegt unter `docs`. Welche Prüfungen beim Import durchgeführt wurden, steht in [VALIDATION.md](VALIDATION.md).

## Herkunft

Hochschul-Teamprojekt der FH Münster. Projektstand ohne ursprüngliche GitLab-Historie; Autorenhinweise im Quellcode sind erhalten.
