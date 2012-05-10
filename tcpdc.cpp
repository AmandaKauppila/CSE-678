/**
  File Transfer Protocal Daemon - Client Side
  The daemon mimics the TCP functionality of the OS using
  the UDP protocal available.
  
  @author Gregory Shayko
  
*/

// Includes-------------------------------------------------------------
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> //gethostbyname

#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "TCP.h"
#include "troll.h"
#include "checksum.h"

/*
 * Implementation
 *  1. Sets up local UDP for incoming send requests
 *  2. Wait for SEND
 *    a. Construct TCP packet
 *    b. Calculate CRC
 *    c. Ready the packet for the Troll
 *    d. Send the packet to the Troll
 *  3. Wait for SEND
 *
 *  Notes:
 *  - Add s0m3?
 */
int main(int argc, char* argv[]){
    
    int sock; //socket descriptor for TCPDC
    struct sockaddr_in sock_tcpdc;//structure for socket name setup
    struct sockaddr_in sock_troll;
    struct sockaddr_in sock_timer;
    struct hostent *troll_host; //sets up locahost troll connection

    tcpd_packet packet_tcpd; //Messages within TCPD
    tcp_packet packet_tcp; //Message for Network
    NetMessage msg; //Message for Troll
    timer_packet packet_timer; //Message for timer
    
    // initialize socket connection
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket");

    debugf("Sockets Opened local=%d", sock);
    
    //construct name of socket to recv/send from
    sock_tcpdc.sin_family = AF_INET;
    sock_tcpdc.sin_addr.s_addr = INADDR_ANY;
    sock_tcpdc.sin_port = htons(TCPD_CLIENT_PORT);

    sock_timer.sin_family = AF_INET;
    sock_timer.sin_addr.s_addr = INADDR_ANY;
    sock_timer.sin_port = htons(TIMER_PORT);
    
    /* Troll Setup */
    if ((troll_host = gethostbyname("localhost")) == NULL) {
	perror("Unknown troll host 'localhost'\n");
	exit(1);
    }
    bzero((char *)&sock_troll, sizeof(sock_troll));
    sock_troll.sin_family = AF_INET;
    bcopy(troll_host->h_addr, (char*)&sock_troll.sin_addr, troll_host->h_length);
    sock_troll.sin_port = htons(TROLL_PORT);

    // bind socket
    if(bind(sock, (struct sockaddr *)&sock_tcpdc, sizeof(sock_tcpdc)) < 0)
	err("Error binding stream socket for tcpd");

    debugf("TCPD locally binded to port %d", sock_tcpdc.sin_port);
    
    int total_read = 0;
    int sequence = 0;
    int iSecret;
    srand ( time(NULL) );
	
    while(1){
	memset(&packet_tcpd, 0, sizeof(tcpd_packet));
	debugf("Waiting for SEND from application");
	total_read = recv(sock, &packet_tcpd, sizeof(tcpd_packet), 0);

	if(packet_tcpd.type == TIMEOUT){
	    debugf("RESEND PACKET %d\n", packet_tcpd.sequence);
	    continue; //wait for next packet
	}

	printf("Received: %d Bytes of Data\n", packet_tcpd.data_len);

	//Compile the data into a tcp packet
	memset(&packet_tcp, 0, sizeof(tcp_packet));
	memcpy(&packet_tcp.data, &packet_tcpd.data, sizeof(packet_tcpd.data));
	packet_tcp.data_len = packet_tcpd.data_len;
		//Set the sequence #
	packet_tcp.sequence = sequence++;

	debugf("Createing Packet %d", packet_tcp.sequence);
	
	//Calculate CRC and place it into the packet
	packet_tcp.checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
	//Ready it for the troll
	memset(&msg, 0, sizeof(NetMessage));
	memcpy(&msg.msg_header, &packet_tcpd.sock_dest, sizeof(sockaddr_in));
	memcpy(&msg.msg_contents, &packet_tcp, sizeof(tcp_packet));

	//Ready it to the timer
	memset(&packet_timer, 0, sizeof(timer_packet));
	packet_timer.sequence = packet_tcp.sequence;
	packet_timer.SEQ = 1;
	packet_timer.timeout = DEFAULT_TIMEOUT;
	if(sendto(sock, (char *)&packet_timer, sizeof(timer_packet), 0, (struct sockaddr *)&sock_timer, sizeof sock_timer) < 0)
	    err("Error sending SEQ to Timer");
	if(sendto(sock, (char *)&msg, sizeof(NetMessage), 0, (struct sockaddr *)&sock_troll, sizeof sock_troll) < 0)
	    err("Error sending to Troll");


	//TESTING ONLY, SEND ACKS Except for 10% time
	//Ready it to the timer
	
	iSecret = rand() % 10 + 1;
	if(iSecret == 1)continue;
	
	memset(&packet_timer, 0, sizeof(timer_packet));
	packet_timer.sequence = packet_tcp.sequence;
	packet_timer.ACK = 1;
	if(sendto(sock, (char *)&packet_timer, sizeof(timer_packet), 0, (struct sockaddr *)&sock_timer, sizeof sock_timer) < 0)
	    err("Error sending ACK to Timer");
	debugf("Sent ACK of %d", packet_timer.sequence);
	
    }
    close(sock);
}

