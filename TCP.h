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
//Common ports used for various processes
#define TCPD_CLIENT_PORT 8010
#define TCPD_SERVER_PORT 8011
#define TCPD_RECV_PORT 8012
#define TIMER_PORT 8013
#define TROLL_PORT 9000

//Used for FTP application
#define MAX_DATA_SIZE 1000

//Timer and window defaults
#define DEFAULT_TIMEOUT 1000
#define MAXRTO 1500
#define WINDOW_SIZE 20

//Common TYPE's used throughout the TCP protocal
#define TYPE_TIMEOUT 1
#define TYPE_SEND 2
#define TYPE_CLOSE 4

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
    uint32_t sequence;
    unsigned sent : 1; //record if sent to receiver
    unsigned acked : 1; //record if acked from receiver
    unsigned short port;
    unsigned long timestamp;
};


/*
 * Timer Packet is what the timer expects to receive.
 * If the ACK flag is set to 1, it indicates a timer
 * should be removed. If SEQ is set to 1 it indicates
 * a timer should be added. The timeout is the RTO
 * provided by the TCPDC.
 */
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
    
    struct sockaddr_in sock_tcpd, localaddr;
    tcpd_packet packet;

    bzero((char *)&localaddr, sizeof localaddr);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = INADDR_ANY;
    localaddr.sin_port = 0;
    
    int sockTmp = socket(AF_INET, SOCK_DGRAM, 0);
    if(bind(sockTmp, (struct sockaddr *)&localaddr, sizeof localaddr) <0)
	err("Failed to bind for SEND");
    
    sock_tcpd.sin_family = AF_INET;
    sock_tcpd.sin_port = htons(TCPD_CLIENT_PORT);
    sock_tcpd.sin_addr.s_addr = INADDR_ANY;
    
    memset(&packet, 0, sizeof(tcpd_packet));
    memcpy(&packet.sock_dest, &name_server, sizeof(sockaddr_in));
    memcpy(&packet.data, buf, len);
    packet.data_len = len;
    packet.type = TYPE_SEND;
    
    //Sleep for 10ms
    usleep(100 * 1000);

    /* Send to the tcpd_c */
    int result = sendto(sockTmp, &packet, sizeof(tcpd_packet), flags, (struct sockaddr *)&sock_tcpd, sizeof(sock_tcpd));

    tcpd_packet packet_in;
    if(recv(sockTmp, (char *)&packet_in, sizeof(tcpd_packet), 0) < 0)
	err("Failed to recv from TCPDC");
    close(sockTmp);
    
    return result;
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
     *  1. Waits for a message from the TCPD
     *  2. Copies the messages data to buf and returns the length
     */
    struct sockaddr_in localaddr;
    /* ... and bind its local address */
    bzero((char *)&localaddr, sizeof localaddr);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = INADDR_ANY;
    localaddr.sin_port = htons(TCPD_RECV_PORT);
    int sockTmp = socket(AF_INET, SOCK_DGRAM, 0);
    if(bind(sockTmp, (struct sockaddr *)&localaddr, sizeof(localaddr)) < 0)
	err("Failed to bind on RECV");	    
    
    /* Wait for data from tcpd */
    tcpd_packet packet;
    memset(&packet, 0, sizeof(tcpd_packet)); //clear packet
    debugf("Waiting for packet on port=%d", localaddr.sin_port);
    if(recv(sockTmp, (char *)&packet, sizeof(tcpd_packet), 0) < 0)
	err("Failed to recv from TCPDS");

    close(sockTmp);
    
    bzero(buf, MAX_DATA_SIZE);
    memcpy(buf, &packet.data, packet.data_len);
    return packet.data_len;
}

int SOCKET(int family, int type, int protocol){
	/*
	* Implementation
	* 1.  Attempts to create socket with UDP function call
	* 2.  If socket is not created, retries creating socket until it is created
	* 3.  To prevent infinite loop, set a timeout, but set it high- try 1000 times-- server side is not connected to our delta timer
	* 4.  return socket ID
	*/
	debugf("in socket!\n");
	int attempts = 0;
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	while ((sock < 0) && (attempts < 1000)){
		attempts++;
		sleep(1);
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		printf("in loop in socket.  attempts = %d\n", attempts);
	}
	
	return sock;
}

int BIND(int sockfd, struct sockaddr *my_addr, socklen_t addrlen){

    /*
     * Implementation
     * 1. assigns port to the unnamed socket
     * 2.  returns 0 on success
     *//*
	 debugf("in bind!\n");
	 int attempts = 0;
	 int addr = bind(sockfd, my_addr, addrlen);
	 while((addr < 0) && (attempts < 1000)){
	 attempts++;
	 sleep(1);
	 addr = bind(sockfd, my_addr, addrlen);
	 printf("in loop in bind.  attempts = %d\n", attempts);
	 }*/
    
    /*
     * Implementation
     *   Tell the TCPD which socket were using
     *   for external communication
     */
    
    struct sockaddr_in sock_tcpd;
    tcpd_packet packet;
    
    sock_tcpd.sin_family = AF_INET;
    sock_tcpd.sin_port = htons(TCPD_SERVER_PORT);
    sock_tcpd.sin_addr.s_addr = INADDR_ANY;
    
    memset(&packet, 0, sizeof(tcpd_packet)); //clear packet
    memcpy(&packet.sock_dest, my_addr, sizeof(sockaddr));
    if(sendto(sockfd, &packet, sizeof(tcpd_packet), 0, (const struct sockaddr *)&sock_tcpd, sizeof(sock_tcpd)) < 0)
	err("Failed to send RECV to TCPDS");
	
    return 1;
    
}

void CLOSE(int sockfd){
    struct sockaddr_in sock_tcpd;
    tcpd_packet packet;
    sock_tcpd.sin_family = AF_INET;
    sock_tcpd.sin_port = htons(TCPD_CLIENT_PORT);
    sock_tcpd.sin_addr.s_addr = INADDR_ANY;
    
    memset(&packet, 0, sizeof(tcpd_packet)); //clear packet
    packet.type = TYPE_CLOSE;
    memcpy(&packet.sock_dest, &name_server, sizeof(sockaddr_in));
    if(sendto(sockfd, &packet, sizeof(tcpd_packet), 0, (const struct sockaddr *)&sock_tcpd, sizeof(sock_tcpd)) < 0)
	err("Failed to send CLOSE to TCPDS");
    
    close(sockfd);
}

int ACCEPT(int sockfd, struct sockaddr *cliaddr, socklen_t *addrlen){
	// not implementing for this project
}

int CONNECT	(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen){
	// not implementing for this project
}

#endif

