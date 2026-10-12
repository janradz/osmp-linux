#== Include ASAN ==#
function(compile_with_asan ENABLE_ASAN)
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
        message(STATUS "AddressSanitizer aktiviert.")
    else()
        message(STATUS "AddressSanitizer deaktiviert.")
    endif()
endfunction()


# Funktionen für GCC Sanitizer-Konfiguration (GCC 12.3)
function(enable_sanitizers SANITIZER_LIST)
    set(VALID_SANITIZERS
        address leak thread undefined
        bounds bool object-size vla-bound
        shift shift-base
        integer-divide-by-zero float-divide-by-zero
        signed-integer-overflow
        pointer-compare pointer-subtract
        returns-nonnull-attribute nonnull-attribute
        vptr
    )

    if(SANITIZER_LIST STREQUAL "none")
        message(STATUS "Sanitizer deaktiviert.")
        return()
    endif()

    string(REPLACE "," ";" SANITIZER_VECTOR "${SANITIZER_LIST}")

    set(SANITIZER_FLAGS "")
    foreach(SAN ${SANITIZER_VECTOR})
        list(FIND VALID_SANITIZERS ${SAN} IS_VALID)
        if(IS_VALID EQUAL -1)
            message(WARNING "Unbekannter oder nicht unterstützter Sanitizer: '${SAN}'. Unterstützte Optionen: ${VALID_SANITIZERS}")
        else()
            list(APPEND SANITIZER_FLAGS ${SAN})
        endif()
    endforeach()

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        list(FIND SANITIZER_FLAGS "address" HAS_ASAN)
        list(FIND SANITIZER_FLAGS "thread" HAS_TSAN)
        if(HAS_ASAN GREATER -1 AND HAS_TSAN GREATER -1)
            message(FATAL_ERROR "GCC unterstützt nicht die gleichzeitige Aktivierung von AddressSanitizer und ThreadSanitizer.")
        endif()
    endif()

    list(JOIN SANITIZER_FLAGS "," SANITIZER_JOINED)
    if(NOT SANITIZER_JOINED STREQUAL "")
        add_compile_options(-fsanitize=${SANITIZER_JOINED} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${SANITIZER_JOINED})
        message(STATUS "Sanitizer aktiviert: ${SANITIZER_JOINED}")
    endif()
endfunction()

# Prüfung gültiger Sanitizer-Name für einfache Auswahl
function(check_sanitizer_compatibility SAN_NAME)
    set(VALID_SANITIZERS none address thread undefined)
    if(NOT SAN_NAME IN_LIST VALID_SANITIZERS)
        message(FATAL_ERROR "Ungültiger Sanitizer: ${SAN_NAME}. Erlaubt: ${VALID_SANITIZERS}")
    endif()

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if(SAN_NAME STREQUAL "none")
            message(WARNING "Achtung: Kein Sanitizer aktiviert. Speicherfehler und Race Conditions bleiben unerkannt.")
        endif()
        if(SAN_NAME STREQUAL "address" OR SAN_NAME STREQUAL "thread")
            message(STATUS "Hinweis: GCC unterstützt AddressSanitizer und ThreadSanitizer nicht gleichzeitig.")
        endif()
    endif()
endfunction()

# Validierung EXECUTABLES-Liste
function(validate_executables)
    set(index 0)
    list(LENGTH EXECUTABLES exec_length)

    while(index LESS exec_length)
        list(GET EXECUTABLES ${index} item)
        if(NOT item STREQUAL "NAME")
            message(FATAL_ERROR "Erwartet 'NAME' an Position ${index}, gefunden '${item}'.")
        endif()
        math(EXPR index "${index} + 1")

        list(GET EXECUTABLES ${index} exe_name)
        if(NOT exe_name OR exe_name STREQUAL "NAME" OR exe_name STREQUAL "SOURCES")
            message(FATAL_ERROR "Fehlender Executable-Name nach 'NAME' an Position ${index}.")
        endif()
        math(EXPR index "${index} + 1")

        list(GET EXECUTABLES ${index} item)
        if(NOT item STREQUAL "SOURCES")
            message(FATAL_ERROR "Erwartet 'SOURCES' nach Executable '${exe_name}' an Position ${index}, gefunden '${item}'.")
        endif()
        math(EXPR index "${index} + 1")

        if(index GREATER_EQUAL exec_length)
            message(FATAL_ERROR "Keine Quellen für Executable '${exe_name}' angegeben.")
        endif()

        list(GET EXECUTABLES ${index} source)
        if(source STREQUAL "NAME" OR source STREQUAL "SOURCES" OR NOT source)
            message(FATAL_ERROR "Keine gültigen Quellen für Executable '${exe_name}' an Position ${index}.")
        endif()

        while(index LESS exec_length)
            list(GET EXECUTABLES ${index} source)
            if(source STREQUAL "NAME")
                break()
            endif()
            if(source STREQUAL "SOURCES")
                message(FATAL_ERROR "Ungültiger Source-Eintrag an Position ${index}.")
            endif()
            math(EXPR index "${index} + 1")
            if(NOT source)
                message(FATAL_ERROR "Unerwartetes Ende der EXECUTABLES-Liste.")
            endif()
        endwhile()
    endwhile()
endfunction()

# Executables hinzufügen
function(call_per_executable NAME)
    add_executable(${NAME} ${ARGN})
    target_link_libraries(${NAME} ${LIBRARIES})
    target_compile_options(${NAME} PRIVATE ${CUSTOM_COMPILE_FLAGS})
    target_compile_definitions(${NAME} PUBLIC _POSIX_C_SOURCE=${POSIX_VERSION})
    if(NOT ({OSMP_RUNNER_NAME} STREQUAL ${NAME}))
        add_dependencies(${OSMP_RUNNER_NAME} ${NAME})
    endif ()

endfunction()

function(create_executables)
    set(index 0)
    list(LENGTH EXECUTABLES length)

    while(index LESS length)
        list(GET EXECUTABLES ${index} item)
        if(item STREQUAL "NAME")
            math(EXPR index "${index} + 1")
            list(GET EXECUTABLES ${index} EXEC_NAME)
            math(EXPR index "${index} + 1")
        endif()

        if(item STREQUAL "SOURCES")
            set(SOURCES "")
            math(EXPR index "${index} + 1")
            while(index LESS length)
                list(GET EXECUTABLES ${index} item)
                if(item STREQUAL "NAME")
                    break()
                endif()
                list(APPEND SOURCES ${item})
                math(EXPR index "${index} + 1")
            endwhile()
            message(STATUS "Erstelle Executable: ${EXEC_NAME} mit Quellen: ${SOURCES}")
            call_per_executable(${EXEC_NAME} ${SOURCES})
        endif()
    endwhile()
endfunction()

