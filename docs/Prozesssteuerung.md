# Architektur der Prozesssteuerung (osmprun)

Das folgende Diagramm beschreibt den internen Kontrollfluss des Runners. Es verdeutlicht die Trennung zwischen der Konfigurationsphase, der Prozess-Instanziierung und dem Ressourcen-Management.

```mermaid
flowchart TD

    %% Terminal Nodes
    Start(["<b>START</b><br/>osmprun main"])
    End_Ok(["<b>EXIT</b><br/>Status: 0"])
    End_Err(["<b>EXIT</b><br/>Status: EXIT_FAILURE"])
    End_Child(["<b>Kindprozess Abbruch (Fehler)</b>"])

    %% Initialisierung & Parsing
    Start --> ArgLoop[Schleife über argv]
    ArgLoop --> ParseInt[["parse_int()"]]
    ParseInt --> CheckFlags{"Mandatory Flags<br/>vorhanden? (-p, -e)"}
    
    CheckFlags -- "Nein" --> PrintErr[Fehlermeldung auf stderr]
    PrintErr --> End_Err
    
    CheckFlags -- "Ja" --> SetDefaults[Log-Defaults setzen]
    SetDefaults --> Alloc[Speicherallokation<br/>osmp_peer_t Array]

    %% Execution Phase (Fork Loop)
    Alloc --> ForkLoop{"Zähler < processcount?"}
    
    ForkLoop -- "Ja" --> ForkProc["fork()"]
    
    ForkProc -- "Kind (== 0)" --> ExecVP["execvp(exec_argv[0], exec_argv)"]
    ExecVP --> ExecFail["execvp Fehler-Log"]
    ExecFail --> End_Child
    
    ForkProc -- "Eltern (> 0)" --> TrackPeer["PID & Status im<br/>Array speichern"]
    TrackPeer --> ForkLoop
    
    ForkProc -- "Fehler (< 0)" --> ForkLog["fork() Fehler-Log"]
    ForkLog --> WaitPhase

    %% Monitoring Phase (Wait Loop)
    ForkLoop -- "Nein" --> WaitPhase{"Index < processcount?"}
    
    WaitPhase -- "Ja" --> WaitPID["waitpid(peers[k].pid, &status, 0)"]
    WaitPID --> EvalExit{"WIFEXITED?"}
    
    EvalExit -- "Ja" --> LogTerm["runner_log():<br/>Terminierung & Exit-Code"]
    EvalExit -- "Nein" --> NextWait[Nächster Peer]
    
    LogTerm --> NextWait
    NextWait --> WaitPhase

    %% Cleanup
    WaitPhase -- "Nein" --> FreeMem["free(peers)"]
    FreeMem --> End_Ok

    %% Styling
    class Start,End_Ok,End_Err,End_Child terminal;
    class ArgLoop,SetDefaults,Alloc,TrackPeer,ExecVP,ExecFail,ForkLog,WaitPID,LogTerm,FreeMem,PrintErr process;
    class CheckFlags,ForkLoop,WaitPhase,EvalExit decision;
    class ParseInt subprogram;

    classDef terminal fill:#f8fafc,stroke:#475569,stroke-width:2px,color:#1e293b;
    classDef process fill:#eff6ff,stroke:#2563eb,stroke-width:1px,color:#1e40af;
    classDef decision fill:#fffbeb,stroke:#d97706,stroke-width:1px,color:#92400e;
    classDef subprogram fill:#f0fdf4,stroke:#16a34a,stroke-width:1px,color:#166534;
```

## Technische Details der Implementierung

### 1. Konfigurations-Logik
Der Runner nutzt eine Schleife über `argv`, um Parameter flexibel zu erfassen. Die Funktion `parse_int` kapselt dabei die `strtol`-Logik inklusive Error-Checking (Overflow, ungültige Zeichen), was die `main` übersichtlich hält.

### 2. Prozess-Lifecycle
*   **Fork-Schleife:** Erzeugt die Prozesse sequentiell. Kritische Fehler beim `fork()` führen nicht zum sofortigen Abbruch des gesamten Runners, sondern erlauben das Einsammeln bereits gestarteter Prozesse.
*   **Exec-Übergang:** Durch `execvp` wird das Abbild des Runners im Kind durch das Zielprogramm ersetzt. Die Argumente ab dem Flag `-e` werden dabei transparent durchgereicht.

### 3. Monitoring & Cleanup
Die `waitpid`-Schleife ist blockierend ausgelegt, um sicherzustellen, dass keine Zombie-Prozesse verbleiben. Erst nach dem erfolgreichen `wait` auf alle Indizes wird der dynamisch allokierte Speicher des Peer-Arrays freigegeben.

### 4. Logging-System
Die Funktion `runner_log` wird zentral für Statusmeldungen genutzt. Sie stellt durch ein verbindliches Format (`Timestamp Verbosity PID Message`) sicher, dass die Logs maschinenlesbar bleiben.
