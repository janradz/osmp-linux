cmake_minimum_required(VERSION 3.20)
project(Betriebssysteme C)

#== Funktionen einbinden ==#
include(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/functions.cmake)

#== Sanitizer Auswahl ==
# Verfügbare Optionen für -DSANITIZER:
#   address   - Speicherfehler wie Use-after-free, Buffer Overflows
#   leak      - Speicherlecks
#   undefined - Undefiniertes Verhalten (z. B. Integer Overflow)
#   memory    - Uninitialisierte Speicherinhalte (nur Clang/ausgewählte Plattformen)
#   thread    - Race Conditions und Thread-Datenrennen (nicht kombinierbar mit address/leak unter GCC!)
#   none      - Deaktiviert alle Sanitizer

#== Sanitizer Flags ==#
# Standard-Sanitizer-Kombination, wenn nichts anderes gesetzt wurde
if(NOT DEFINED SANITIZER)
   set(SANITIZER "address,leak,undefined"  CACHE STRING "Sanitizer aktivieren (none,address,leak,undefined,thread,...)")
   # set(SANITIZER "address,undefined,leak,bounds,bool,vla-bound"  CACHE STRING "Sanitizer aktivieren (none,address,leak,undefined,bounds,bool,...)")
   # set(SANITIZER "thread")  # Achtung: TSAN ist unter GCC nicht mit ASAN kombinierbar!
endif()

message(STATUS "Gewählter Sanitizer: ${SANITIZER}")

# Aktivierung + Validierung + Kompatibilitätsprüfung in einer Funktion
enable_sanitizers(${SANITIZER})


#== Globale Compiler-Definitionen für alle Targets ==#
set(POSIX_VERSION "200809L")
add_compile_definitions(_POSIX_C_SOURCE=${POSIX_VERSION} _DEFAULT_SOURCE)


#== Compiler Flags ==#
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  
set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Werror -Wextra -Wconversion -g")

#== Add Library target ==#
set(LIBRARY_SRC_FILES
    src/osmpLibrary/OSMP.h
    src/osmpLibrary/osmpLib.h
    src/osmpLibrary/osmpLib.c
)
add_library(libosmp STATIC ${LIBRARY_SRC_FILES})


#== Define Libraries ALL executables link against ==#
set(LIBRARIES  pthread  libosmp )


#== Executables ==#
set(OSMP_RUNNER_NAME osmprun)
set(EXECUTABLES
    NAME ${OSMP_RUNNER_NAME}
    SOURCES src/osmpRunner/osmpRun.c
    NAME echoall
    SOURCES src/osmpExecutables/echoAll.c
        NAME logger
        SOURCES src/osmpExecutables/logger.c
        NAME readshm
        SOURCES src/osmpExecutables/readshm.c
        NAME senderreceiver
        SOURCES src/osmpExecutables/senderReceiver.c
        NAME test_barrier
        SOURCES src/osmpExecutables/test_barrier.c
        NAME gatherer
        SOURCES src/osmpExecutables/gatherer.c
        NAME senderdeadlocktest
        SOURCES src/osmpExecutables/senderDeadlockTest.c
        NAME asyncTest
        SOURCES src/osmpExecutables/asyncTest.c
        NAME circular_stress
        SOURCES src/osmpExecutables/circular_stress.c
        NAME storm_test
        SOURCES src/osmpExecutables/storm_test.c
        NAME leak_test
        SOURCES src/osmpExecutables/leak_test.c
        NAME crash_test
        SOURCES src/osmpExecutables/crash_test.c
        NAME finalizeTest
        SOURCES src/osmpExecutables/finalizeTest.c
    # Neue Executables hier einfügen
)

validate_executables()
create_executables()
