#!/bin/bash

# Pfade zu den Binaries
RUNNER="./build/osmprun"
ECHO="./build/echoall"

echo "=== OSMP Runner Tests ==="

# 1. Start-Szenarien
$RUNNER -p 1 -e $ECHO hello > /dev/null && echo "[PASS] Single start"
$RUNNER -p 500 -e $ECHO test > /dev/null && echo "[PASS] Multi start (500)"

# 2. Fehlerhafte Parameter
$RUNNER -p 0 -e $ECHO > /dev/null 2>&1 || echo "[PASS] Invalid process count (0) rejected"
$RUNNER -p 1 > /dev/null 2>&1 || echo "[PASS] Missing executable flag (-e) rejected"

# 3. Logging Tests
rm -f osmp_default.log test.log

# Test 1: Kein Log-Flag -> kein File
$RUNNER -p 1 -e $ECHO test > /dev/null
[ ! -f osmp_default.log ] && echo "[PASS] Logging-Test 1: No log file created without flags"

# Test 2: Logdatei ohne Level -> Default Level 1
$RUNNER -p 1 -l test.log -e $ECHO test > /dev/null
[ -f test.log ] && echo "[PASS] Logging-Test 2: Log file created with -l flag"

# Test 3: Logdatei mit explizitem Level 2
rm -f test.log
$RUNNER -p 1 -l test.log -v 2 -e $ECHO test > /dev/null
[ -f test.log ] && echo "[PASS] Logging-Test 3: Log file created with -v 2 flag"

# 4. Abbruch-Szenarien
# Wir prüfen, ob die Fehlermeldung auf stderr ausgegeben wird
$RUNNER -p 1 -e ./gibts_nicht 2>&1 | grep -q "execvp failed" && echo "[PASS] Error message for invalid executable found"

echo "=== Tests Finished ==="
