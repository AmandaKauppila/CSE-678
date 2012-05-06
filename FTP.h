/*
 * Includes definitions specific to the
 * file transfer protocal application.
 *
 * @author Gregory Shayko (Gregor)
 */

// Includes-------------------------------------------------------------
// N/A

// Defines--------------------------------------------------------------
//How many bytes of the filename to hold
#define MAX_FILENAME_SIZE 20
#define FILENAME_PREFIX "ftp_"

// Header Guard
#ifndef FTP_H_GUARD
#define FTP_H_GUARD


/**
 * Defines the first packet that is sent to
 * the ftp server. The first packet always
 * contains the 4-byte file size and name.
 * 
 * @author Gregor
 */
typedef struct ftp_header {
    uint32_t size;
    char name[MAX_FILENAME_SIZE];
} ftp_header;


#endif
