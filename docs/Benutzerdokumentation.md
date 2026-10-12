# Benutzerdokumentation - OSMP Projekt

Diese Dokumentation beschreibt die Verwendung der Programme `echoall` und `osmprun`, sowie die Einrichtung der Build-Umgebung und das Logging-System.

## 1. Programme

### echoall
`echoall` ist ein Hilfsprogramm zur Überprüfung der Prozessidentifikation und Argumentübergabe.
- **Funktion:** Gibt alle übergebenen Kommandozeilenargumente nummeriert aus. Anschließend gibt es 10 Mal in Sekundenintervallen die eigene Prozess-ID (PID), die Parent-Prozess-ID (PPID) und die Thread-ID (TID) aus.
- **Zweck:** Testen, ob der Runner Argumente korrekt weitergibt und ob die Prozesshierarchie stimmt.

### osmprun
`osmprun` ist der zentrale Prozess-Starter (Runner) für OSMP-Anwendungen.
- **Funktion:** Startet eine konfigurierbare Anzahl von Kindprozessen eines angegebenen Executables.
- **Verhalten:** Erzeugt mittels `fork()` die Prozesse und führt das Zielprogramm mittels `execvp()` aus. Der Runner wartet, bis alle Kindprozesse beendet wurden.

---

## 2. Build-Umgebung (CMake)

Das Projekt verwendet CMake als Build-System-Generator.

### Voraussetzungen
- GCC (GNU Compiler Collection)
- CMake (Version 3.20 oder höher)

### Build-Befehle
Um das Projekt zu übersetzen, führen Sie folgende Befehle im Projektstammverzeichnis aus:

```bash
# 1. Konfiguration des Projekts (erzeugt das build-Verzeichnis)
cmake -S . -B build -D CMAKE_BUILD_TYPE=Debug

# 2. Kompilieren der Programme
cmake --build build
```

Die fertigen Binaries (`echoall` und `osmprun`) befinden sich nach dem Build im Ordner `build/`.

---

## 3. Aufrufbeispiele und Parameter

### Aufruf von osmprun
Der Runner wird mit folgenden Parametern gesteuert:

| Parameter | Beschreibung |
| :--- | :--- |
| `-p <n>` | **(Pflicht)** Anzahl der zu startenden Prozesse. |
| `-e <cmd> [args...]` | **(Pflicht)** Das auszuführende Programm und dessen Argumente. Dieser Parameter muss der letzte in der Liste sein. |
| `-l <file>` | Pfad zur Logdatei (optional). |
| `-v <level>` | Loglevel (0-3) für die Ausführlichkeit (optional). |

#### Beispiele:

**Einfacher Start:**
Startet 4 Instanzen von `echoall` mit dem Argument "Hallo":
```bash
./build/osmprun -p 4 -e ./build/echoall Hallo
```

**Mit Logging:**
Startet 2 Instanzen und schreibt Logs in `mein_test.log` mit Loglevel 2:
```bash
./build/osmprun -p 2 -l mein_test.log -v 2 -e ./build/echoall Test
```

---

## 4. Hinweise zum Logging

Das Logging-System in `osmprun` dokumentiert den Lebenszyklus der Prozesse.

### Aktivierung
- Das Logging wird durch die Angabe von `-l <Dateiname>` oder `-v <Level> > 0` aktiviert.
- Wenn nur `-l` angegeben wird, ist das Standard-Loglevel **1**.
- Wenn nur `-v` (größer 0) angegeben wird, wird die Standard-Logdatei **`osmp_default.log`** verwendet.

### Loglevel
Die Ausführlichkeit wird über Ganzzahlen gesteuert:
- **0:** Logging deaktiviert (außer bei Fehlern auf stderr).
- **1:** Standard-Logs (Start/Stopp von Prozessen).
- **2-3:** Erhöhte Ausführlichkeit (je nach Implementierungsstand der Library).

### Format der Logdatei
Jeder Eintrag in der Logdatei folgt diesem Schema:
`[Zeitstempel] [Loglevel] [PID] [Nachricht]`

Beispiel:
`2026-04-15T14:30:05 1 12345 [RUNNER] Starte 4 Peers mit Executable './build/echoall'.`
