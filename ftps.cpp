/*
 * Server Side Code for ftps
 * 
 * @author Gregory Shayko
 */
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include <string>
#include <strings.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "FTP.h"
#include "TCP.h"
#include "utils.h"

using namespace std;

int main(int argc, char* argv[]){

    int sock; // initial socket descriptor 
    struct sockaddr_in sock_tcp; // sock for tcp connection
    char buf[1000]; // buffer for holding read data 
  
    if (argc < 2)
	err("usage: %s <local-port>", argv[0]);

    /*initialize socket connection in unix domain*/
    if((sock = SOCKET(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket");

    debugf("Created socket = %d", sock);
    
    /* The local info talk to TCPD */
    sock_tcp.sin_family = AF_INET;
    sock_tcp.sin_addr.s_addr = INADDR_ANY;
    sock_tcp.sin_port = htons(atoi(argv[1]));
    
    /* Bind FTPS to Local TCPD */
    if(BIND(sock, (struct sockaddr *)&sock_tcp, sizeof(struct sockaddr_in)) < 0)
	err("Error binding stream socket");
    
    bzero(buf, 1000);
    char file_name[255];
    int file_size = 0;
    
    ftp_header packet_header;
    memset(&packet_header, 0, sizeof(ftp_header));

    printf("FTP Server started, waiting on port %d...", name_server.sin_port);
    
    // Get the size and file_name 
    if(RECV(sock, buf, 24, 0) < 0)
	err("Error on stream socket");
    
    debugf("Received Packet: %s", buf);

    memcpy(&packet_header, &buf, sizeof(ftp_header));
    file_size = (int)packet_header.size;

    // Prefix with ftp
    // Replace all / with dashes
    strcpy(file_name, FILENAME_PREFIX);
    strncat(file_name, packet_header.name, MAX_FILENAME_SIZE);
    replace(file_name, file_name+strlen(file_name), '/', '-');
    
    debugf("File size=%d and name='%s':", file_size, file_name);
    
    printf("FTP File Transfer Initiated\n");
    printf("  File Size: %d KB\n", file_size);
    printf("  File Name: %s \n", packet_header.name);
    
    debugf("Creating file %s", file_name);
    
    FILE *file;
    
    //Open the file to write to
    file = fopen(file_name, "wb");
    if (file==NULL)
	err("Error in opening file.");		
    
    int total_read = 0;
    int total = 0;
    
    /* 
     * Read the file being sent from the client
     * writing it to the new file at the same time
     */
    
    while(1){
	bzero(buf, 1000);
	total_read = RECV(sock, buf, 1000, 0);
	total = total + total_read;
	debugf("%d of %d written", total, file_size);
	fwrite(buf, 1, total_read, file);	
	if(total_read < 1000 || total >=  file_size)
	    break;
    }

    printf("File transfer complete. File saved to:\n  %s", file_name);
    fclose(file);
    close(sock);
    
}
