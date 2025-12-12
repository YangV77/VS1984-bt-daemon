//
// Created by willyy on 2025-12-12.
//

#include "bt_utils.h"
#include <dirent.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "../ver/version.h"
#include "../ver/build_version.h"
int d = 0;
static int debug(){
    return d;
}

const char *rsx_version(){
    return RSUNX_VERSION;
}

const char *build_version(){
    return BUILD_VERSION;
}

void set_debug(int dbg){
    d = dbg;
}

#define LOG_BUFFER_SIZE 512000
static const char *timestr() {
    time_t t;
    time(&t);
    struct tm *timeinfo = localtime(&t);

    thread_local static char str[20];
    strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", timeinfo);

    return str;
}

void iloge(const char *format, ...) {
    if (debug()<1) {
        return;
    }

    if (format == NULL) return;

    char buffer[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);

    int ret = vsnprintf(buffer, LOG_BUFFER_SIZE, format, args);
    va_end(args);

    if (ret < 0 || ret >= LOG_BUFFER_SIZE) {

        fprintf(stderr, "iloge error: failed to format iloge message or message too long.\n");
        return;
    }
    fprintf(stderr, "<%s> %s\n", timestr(), buffer);
}