# Prüfung am 8. Oktober 2026

- Runner, echoall, senderreceiver, test_barrier und gatherer mit GCC direkt gebaut.
- Flags: C11, POSIX 200809L, _DEFAULT_SOURCE, -pedantic -Wall -Werror -Wextra -Wconversion -g -pthread.
- Alle fünf Builds erfolgreich.
- Laufzeittests: echoall, senderreceiver, test_barrier und gatherer mit jeweils zwei Prozessen; alle Exitcode 0, keine stderr-Ausgabe.
- CMake selbst war in der Prüfumgebung nicht installiert; der CMake-Build wurde nicht ausgeführt.
- Keine Prüfung aller Stress-/Crash-/Deadlock-Szenarien; kein Nachweis genereller Race-Freiheit.
- Logs, PDF-Dateien und Bilder der Originalabgabe sind nicht Teil der Portfolio-Kopie.
