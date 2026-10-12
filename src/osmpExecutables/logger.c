//
// Created by julian on 29.04.26.
//

#include <stdio.h>

#include "../osmpLibrary/OSMP.h"

int main(const int argc, char** argv)
{
    if (OSMP_Init(&argc, &argv) != OSMP_SUCCESS) {
        fprintf(stderr, "Error in OSMP_Init\n");
    }
    fprintf(stdout, "logging osmp:\n");
    OSMP_Log(OSMP_LOG_BIB, "[Logger] A level one logging Message");
    OSMP_Log(OSMP_LOG_SYS, "[Logger] A level two logging Message");
    OSMP_Log(OSMP_LOG_ALL, "[Logger] A level 3 logging Message");

    OSMP_Finalize();
    return 0;
}