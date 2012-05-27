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
#include "circular.h"

using namespace std;

void createAndSendTcpPacket(tcpd_packet packet_tcpd);
void startTimer(uint32_t sequence, unsigned int timeout);
void ackTimer(uint32_t sequence);

typedef circular_buffer<tcpd_packet> cbuf_tcp;
cbuf_tcp cbuf(64);

int sock; //socket descriptor for TCPDC
struct sockaddr_in sock_tcpdc;//structure for socket name setup
struct sockaddr_in sock_from;
struct sockaddr_in sock_troll;
struct sockaddr_in sock_timer;
struct hostent *troll_host; //sets up locahost troll connection
socklen_t fromlen;
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
    
    tcp_packet packet_tcp; //Message for Network
    NetMessage msg; //Message for Troll
    char buffer[MAXNETMESSAGE];//Holds the initial data from socket.

    unsigned int counter = 0;//Common counter for all purposes.
    cbuf_tcp::iterator it = cbuf.begin();
	
    // initialize socket connection
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket");

    int n = 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
    
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
    srand(time(NULL));
	
    while(1){
	/*
	 * Implementation
	 * The loop takes different directions based on if it is
	 * a tcp from troll or a tcpd from TCP or timer
	 *  1. TCP
	 *     An ACK
	 *  2. TCPD
	 *    a. SEND
	 *    b. TIMEOUT
	 *    c. CLOSE
	 */
	printf("\n");debugf("Waiting for a packet...");
	fromlen = sizeof(fromlen);
	total_read = recvfrom(sock, &buffer, sizeof(buffer), 0, (sockaddr *)&sock_from, &fromlen);

	//if from the troll its a TCP packet, else TCPD packet
	if(sock_from.sin_port == sock_troll.sin_port){
	    	
	    debugf("TCP size=%d, port=%d", total_read, sock_from.sin_port);

	    NetMessage msg;//Get the NetMessage
	    memset(&msg, 0, sizeof(NetMessage));
	    memcpy(&msg, &buffer, sizeof(NetMessage));
	    
	    memset(&packet_tcp, 0, sizeof(tcp_packet));
	    memcpy(&packet_tcp, &msg.msg_contents, sizeof(tcp_packet));

	    // This should be a ACK and nothing else, we must check
	    // the CRC first
	    //Check the CHECKSUM
	    unsigned int checksum_in = packet_tcp.checksum;
	    packet_tcp.checksum = 0;
	    unsigned int checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
	    debugf("Checksum = %d vs Calculated = %d", checksum_in, checksum);
	
	    if(checksum != checksum_in){
		printf("CHECKSUM ERROR, Seq=%d\n", packet_tcp.sequence);
		//drop the packet
		continue;
	    }

	    ackTimer(packet_tcp.sequence);

	    //slide the timer over if possible
	    it = cbuf.begin();
	    while (it != cbuf.end()){
		if((*it).acked == 1){
		    cbuf.pop_front();
		}else{
		    break;
		}
	    }
	    
	}else{ //tcpd packet
	    debugf("TCPD size=%d, port=%d", total_read, sock_from.sin_port);
	    tcpd_packet packet_tcpd; //Messages within TCPD
	    memset(&packet_tcpd, 0, sizeof(tcpd_packet));
	    memcpy(&packet_tcpd, &buffer, sizeof(packet_tcpd));

	    //If the packets a timeout, resend it
	    if(packet_tcpd.type == TYPE_TIMEOUT){
		debugf("TIMEOUT: PACKET %d", packet_tcpd.sequence);
		//TODO Find the packet in the window, and resend it
		it = cbuf.begin();counter = 1;
		while (it != cbuf.end() && counter <= WINDOW_SIZE){
		    if((*it).sequence == packet_tcpd.sequence){
			createAndSendTcpPacket((*it));
			break;
		    }
		    ++it;counter++;
		}
		continue; //wait for next packet
	    }

	    //IF the packet is a CLOSE, send a FIN
	    if(packet_tcpd.type == TYPE_CLOSE){
		debugf("CLOSE Connection");
		//TODO close the connection
		continue;
	    }

	    debugf("Contained %d Bytes of Data\n", packet_tcpd.data_len);
	    
	    //Place it in the buffer
	    if(cbuf.size() == cbuf.capacity()){
		//if the buffer is full, must force SEND to wait
		//until it frees up. To do this via implementation we
		//should set a flag and location to hold the data that
		//needs to be buffered. When the buffer frees up we
		//push the data on the end and tell the SEND function
		//were ready for more.

		//TODO implement

		
		debugf("BUFFER WAS FULL");
	    }else{
		packet_tcpd.acked = 0;
		packet_tcpd.sent = 0;
		packet_tcpd.sequence = sequence++;
		cbuf.push_back(packet_tcpd);
		//Tell SEND it was succefull.
	    }
	    //send everthing currently not sent
	    it = cbuf.begin();counter = 1;
	    while (it != cbuf.end() && counter <= WINDOW_SIZE){
		if((*it).sent == 0){
		    createAndSendTcpPacket((*it));
		    (*it).sent = 1;
		}
		++it;counter++;
	    }
	}
    }
    close(sock);
}

void startTimer(uint32_t sequence, unsigned int timeout){
    //Ready it to the timer
    timer_packet packet_timer;
    memset(&packet_timer, 0, sizeof(timer_packet));
    packet_timer.sequence = sequence;
    packet_timer.SEQ = 1;
    //TODO change this to the calculated RTO
    packet_timer.timeout = timeout;
    if(sendto(sock, (char *)&packet_timer, sizeof(timer_packet), 0, (struct sockaddr *)&sock_timer, sizeof sock_timer) < 0)
	err("Error sending SEQ to Timer");
}


void ackTimer(uint32_t sequence){
    timer_packet packet_timer; //Message for timer
    memset(&packet_timer, 0, sizeof(timer_packet));
    packet_timer.sequence = sequence;
    packet_timer.ACK = 1;
    if(sendto(sock, (char *)&packet_timer, sizeof(timer_packet), 0, (struct sockaddr *)&sock_timer, sizeof sock_timer) < 0)
	err("Error sending ACK to Timer for SEQ=%d");
    debugf("Sent ACK of %d to timer", packet_timer.sequence);
}

void createAndSendTcpPacket(tcpd_packet packet_tcpd){
    
    tcp_packet packet_tcp;

    usleep(1 * 1000);//prevents UDP bffr overflow
    
    //Compile the data into a tcp packet
    memset(&packet_tcp, 0, sizeof(tcp_packet));
    memcpy(&packet_tcp.data, &packet_tcpd.data, sizeof(packet_tcpd.data));
    packet_tcp.data_len = packet_tcpd.data_len;
	
    //Calculate CRC and place it into the packet
    packet_tcp.checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);

    //Set some additional TCP fields
    packet_tcp.sequence = packet_tcpd.sequence;
    packet_tcp.source_port = TCPD_SERVER_PORT;
    packet_tcp.destination_port = packet_tcpd.sock_dest.sin_port;
	
    //Ready it for the troll
    NetMessage msg;
    memset(&msg, 0, sizeof(NetMessage));
    memcpy(&msg.msg_header, &packet_tcpd.sock_dest, sizeof(sockaddr_in));
    memcpy(&msg.msg_contents, &packet_tcp, sizeof(tcp_packet));

    startTimer(packet_tcp.sequence, 5000);
    
    if(sendto(sock, (char *)&msg, sizeof(NetMessage), 0, (struct sockaddr *)&sock_troll, sizeof sock_troll) < 0)
	err("Error sending to Troll seq=%d", packet_tcp.sequence);

    debugf("SENT seq=%d, port=%d", packet_tcp.sequence, packet_tcp.destination_port);

    packet_tcpd.sent = 1;
}
