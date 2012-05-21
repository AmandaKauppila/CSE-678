/*
 * This file includes a number of general helper functions
 * to be used throughout the application.
 *
 * @author Gregory Shayko (Gregor)
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h> //Used for multiple args

// Header Guard
#ifndef UTILS_H_GUARD
#define UTILS_H_GUARD

#define DEBUG_ENABLED true //Enable Debug Messages?

/*
 * Prints the error message and exits the program.
 *
 * @author Gregor
 */
void err(const char *msg, ...){
    va_list args;
    va_start( args, msg );
    vfprintf( stderr, msg, args );
    va_end( args );
    perror("\nERROR:");
    fflush(stderr);
    exit(1);
}

/**
 * Prints the debug message. This function allows
 * the ability to easily turn on/off all debug
 * messages via the DEBUG_EnABLED definition.
 *
 * @author Gregor
 */

void debugf(const char *format, ...){
    if(!DEBUG_ENABLED)return;
    va_list args;
    fprintf( stdout, "[Debug] " );
    va_start( args, format );
    vfprintf( stdout, format, args );
    va_end( args );
    fprintf( stdout, "\n" );
    fflush(stdout);
}



#endif
