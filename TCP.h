/*
 * TCP.h provides the necessary functions and structures
 * in order to use the custom TCP protocal. A complete
 * defenition of how the protocal works can be found
 * within the provided documentation.
 *
 * @author Gregory Shayko
 */

// Includes-------------------------------------------
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "utils.h"

// Header Guard
#ifndef TCP_H_GUARD
#define TCP_H_GUARD

// Defines --------------------------------------------
#define TCPD_CLIENT_PORT 9090
#define TCPD_SERVER_PORT 9091
#define TIMER_PORT 8592
#define TROLL_PORT 8593
#define MAX_DATA_SIZE 1000
#define DEFAULT_TIMEOUT 3000

#define TIMEOUT 5

//Specifies the address and port of the server. Global
//since accept and connect are not functioning yet.
struct sockaddr_in name_server;

/*
 * The TCPD Packet is the main form of communication
 * between the TCP functions and TCP Daemon.
 */
typedef struct tcpd_packet {
    unsigned type : 3;
    struct sockaddr_in sock_dest; //General socket descriptor
    unsigned int data_len; //Length of data actually used
    char data[MAX_DATA_SIZE]; //Message Data
    unsigned int sequence;
};

typedef struct timer_packet {
    unsigned ACK : 1;
    unsigned SEQ : 1;
    unsigned int sequence;
    unsigned int timeout;
};
/*
 * TCP Packet is what is sent over the network. It mimics
 * the header from the TCP Protocal.
 */
typedef struct tcp_packet {
    uint16_t source_port; //16 bit source port
    uint16_t destination_port; //16 bit destination port
    uint32_t sequence; //32 bit sequenct number
    uint32_t ack; //32 bit acknowledgment number
    unsigned offset : 4;
    unsigned reserved : 3;
    unsigned NS : 1;
    unsigned CWR : 1;
    unsigned ECE : 1;
    unsigned URG : 1;
    unsigned ACK : 1;
    unsigned PSH : 1;
    unsigned RST : 1;
    unsigned SYN : 1;
    unsigned FIN : 1;
    uint16_t window_size; //16 bit window size
    uint16_t checksum; //16 bit CRC checksum code to verify the data
    uint16_t urgent; //Set if the packet is urgent
    int data_len; //Length of data actually used
    char data[MAX_DATA_SIZE]; //Message Data
};


/**
 * Send a message on a socket via custom TCP protocal
 *
 * @param sockfd socket file descriptor to use
 * @param buf location of the message to send
 * @param len the length of the buf message
 * @flags any flags that should be included
 *
 * @author Gregory Shayko
 */
size_t SEND(int sockfd, void *buf, size_t len, int flags){

    /*
     * Implementation
     *  1. Copies the data from buf into a a tcpd packet.
     *  2. Packet is sent to the tcpd daemon.
     *
     *  Notes:
     *  - A 10ms wait helps the troll from screwing up.
     *  - The packet contains the destination data, buf data,
     *    and length of the data.
     */
    
    struct sockaddr_in sock_tcpd;
    tcpd_packet packet;
    
    sock_tcpd.sin_family = AF_INET;
    sock_tcpd.sin_port = htons(TCPD_CLIENT_PORT);
    sock_tcpd.sin_addr.s_addr = INADDR_ANY;
    
    memset(&packet, 0, sizeof(tcpd_packet));
    memcpy(&packet.sock_dest, &name_server, sizeof(sockaddr_in));
    memcpy(&packet.data, buf, len);
    packet.data_len = len;
    
    //Sleep for 10ms
    usleep(10 * 1000);
    
    /* Send to the tcpd_c */
    return sendto(sockfd, &packet, sizeof(tcpd_packet), flags, (struct sockaddr *)&sock_tcpd, sizeof(sock_tcpd));
}

/**
 * Receive a message from a socket via custom TCP protocal
 *
 * @param sockfd socket file descriptor to use
 * @param buf location of where the message should be stored
 * @param len the length of the incoming buf message
 * @flags any flags that should be included
 */
size_t RECV(int sockfd, void *buf, size_t len, int flags){
     /*
     * Implementation
     *  1. Tells the TCPD on which port to RECV on via a UDP message
     *  2. Waits for a message from the TCPD
     *  3. Copies the messages data to buf and returns the length
     *
     *  Notes:
     *  - sock_dest of the tcpd packet is used to instruct the tcpd
     *    on which port to listen on
     */
    struct sockaddr_in sock_tcpd;
    tcpd_packet packet;
    
    sock_tcpd.sin_family = AF_INET;
    sock_tcpd.sin_port = htons(TCPD_SERVER_PORT);
    sock_tcpd.sin_addr.s_addr = INADDR_ANY;
    
    memset(&packet, 0, sizeof(tcpd_packet)); //clear packet
    memcpy(&packet.sock_dest, &name_server, sizeof(sockaddr_in));
    if(sendto(sockfd, &packet, sizeof(tcpd_packet), flags, (const struct sockaddr *)&sock_tcpd, sizeof(sock_tcpd)) < 0)
	err("Failed to send RECV to TCPDS");

    /* Wait for data from tcpd */
    memset(&packet, 0, sizeof(tcpd_packet)); //clear packet
    if(recv(sockfd, (char *)&packet, sizeof(tcpd_packet), 0) < 0)
	err("Failed to recv from TCPDS");
    //debugf("RECV Received len=%d", packet.data_len);
    bzero(buf, MAX_DATA_SIZE);
    memcpy(buf, &packet.data, packet.data_len);
    return packet.data_len;
}

int SOCKET(	int family, int type, int protocol){
	/*
	* Implementation
	* 1.  Attempts to create socket with UDP function call
	* 2.  If socket is not created, retries creating socket until it is created
	* 3.  To prevent infinite loop, set a timeout, but set it high- try 1000 times-- server side is not connected to our delta timer
	* 4.  return socket ID
	*/
	printf("in socket!\n");
	int attempts = 0;
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	while ((sock < 0) && (attempts < 1000)){
		attempts++;
		sleep(1);
		sock = SOCKET(AF_INET, SOCK_DGRAM, 0);
		printf("in loop in socket.  attempts = %d\n", attempts);
	}
	
	return sock;
}

int BIND (int sockfd, struct sockaddr *my_addr, socklen_t addrlen){
	/*
	* Implementation
	* 1. assigns port to the unnamed socket
	* 2.  returns 0 on success
	*/
	printf("in bind!\n");
	int attempts = 0;
	int addr = bind(sockfd, my_addr, addrlen);
	while((addr < 0) && (attempts < 1000)){
		attempts++;
		sleep(1);
		addr = bind(sockfd, my_addr, addrlen);
		printf("in loop in bind.  attempts = %d\n", attempts);
	}
	
	return addr;
}

int ACCEPT(int sockfd, struct sockaddr *cliaddr, socklen_t *addrlen){
	// not implementing for this project
}

int CONNECT	(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen){
	// not implementing for this project
}
#endif

