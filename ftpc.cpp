/*
 * Client Side Code for ftpc
 * 
 * @author Gregory Shayko (Gregor)
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> //gethostbyname requires -lxnet

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "FTP.h"
#include "TCP.h"
#include "utils.h"

using namespace std;

int main(int argc, char* argv[]){

    int sock;                     /* initial socket descriptor */
    int rval;                    /* returned value from a read */
    struct sockaddr_in sin_addr; /* structure for socket name setup*/

    char buf[1000];     /* message to sent to server */
    struct hostent *hp;
    
    FILE *fp;

    //Check that the required number of
    //arguments are met before proceeding
    if(argc < 3)
	err("usage: ftpc <remote-IP> <remote-port> <local-file-to-transfer>\n");
    
    //Open the file as read-only binary
    fp = fopen(argv[3], "rb");
    if (fp==NULL)
	err("The file specified could not be loaded\n");
    
    /* initialize socket connection in unix domain */
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error opening datagram socket\n");

    hp = gethostbyname(argv[1]);
    if(hp == 0) {
	fprintf(stderr, "%s: unknown host\n", argv[1]);
	exit(2);
    }

    /* construct name of socket to send to */
    bcopy((char *)hp->h_addr, (char *)&name_server.sin_addr, hp->h_length);
    name_server.sin_family = AF_INET;
    name_server.sin_port = htons(atoi(argv[2])); /* fixed by adding
					       * htons() */
    /*
     * 1. Calculate and send the file size in bytes
     *    As the first 4 bytes
     * 2. Send the filename as the next 20 bytes
     * 3. Send the file
     *
     *  NO DATA CAN BE GREATER THAN 1000 bytes
     * 
     */
    struct stat filestatus;
    ftp_header packet_header;
    memset(&packet_header, 0, sizeof(ftp_header));
 
    stat(argv[3], &filestatus);
    strcpy(packet_header.name, argv[3]);

    packet_header.size = (uint32_t)filestatus.st_size;
    
    debugf("File size: %4d", packet_header.size);
    debugf("File name: %s", packet_header.name);

    memcpy(&buf, &packet_header, sizeof(ftp_header));
    
    int total_read, total_sent, cumalative_read, cumalative_sent;

    cumalative_read = 0;

    total_sent = SEND(sock, buf, 24, 0);
    if(total_sent < 0)err("Socket connection lost");
    cumalative_sent = total_sent;

    while(!feof(fp)){
	bzero(buf, 1000);
	total_read = fread(buf, sizeof(char), sizeof(buf), fp);
	cumalative_read += total_read;
	debugf("\nRead %d of %d", cumalative_read, filestatus.st_size);
	if(total_read < 0)
	    err("Unable to read the file during sending");
	total_sent = SEND(sock, buf, total_read, 0);
	if(total_sent < 0)
	    err("Socket connection lost");
	cumalative_sent += total_sent;
	debugf("\nSent %d of %d", cumalative_sent, filestatus.st_size+20+4);	 
    }
    
    printf("File '%s' Sent.\n", packet_header.name);

    return 0;
}
