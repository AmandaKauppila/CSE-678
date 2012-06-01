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
#include "timeval.h"

using namespace std;

void createAndSendTcpPacket(tcpd_packet packet_tcpd);
void createAndSendTcpFinPacket(unsigned int seq, sockaddr_in destination);
void createAndSendTcpAckPacket(unsigned int ack, sockaddr_in destination);
void startTimer(uint32_t sequence, unsigned int timeout);
void ackTimer(uint32_t sequence);
unsigned long rtoCalc(unsigned long rtt);
    
typedef circular_buffer<tcpd_packet> cbuf_tcp;
cbuf_tcp cbuf(64);

int sock; //socket descriptor for TCPDC
struct sockaddr_in sock_tcpdc;//structure for socket name setup
struct sockaddr_in sock_from;
struct sockaddr_in sock_troll;
struct sockaddr_in sock_timer;
struct hostent *troll_host; //sets up locahost troll connection
socklen_t fromlen;

unsigned long rto = DEFAULT_TIMEOUT;
double rto_A = 0;
double rto_D = 3;


/*
 * Implementation
 *  1. Sets up local UDP for incoming send requests
 *  2. When a packet is received it does one of 2 things
 *  3. If ITS A TCP Packets
 *    a. Check the checksum
 *    b. Inform the timer if its an ACK
 *    c. Slide the window if required
 *  4. IF ITS A TCPD Packet
 *    a. If ITS A TIMEOUT
 *       i. Resend the packet
 *      ii. Restart the timer
 *    b. IF ITS A CLOSE Call
 *       i. Send the FIN
 *      ii. Once the FINACK is received send ack
 *     iii. After certain time close
 *    c. ELSE ITS Packet to send
 *       i. If the buffer is full wait to add
 *          the packet until it opens up. Making send
 *          wait till its gets placed.
 *       i. Add the packet to the buffer for sending
 *      ii. Tell send were ready for another packet
 *  5. Check if the buffer has any packets to send in
 *     the window
 *    a.If so, send and start their timer.
 *  6. Move the window if required.
 *  
 */
int main(int argc, char* argv[]){
    
    tcp_packet packet_tcp; //Message for Network
    tcpd_packet packet_tcpd_full;
    struct sockaddr_in sock_from_full, sock_fin;
    bool buff_full = false;
    bool closing = false, fin_sent = false;
    unsigned int seq_close;
    int fin_timeout = 0;
    
    NetMessage msg; //Message for Troll
    char buffer[MAXNETMESSAGE];//Holds the initial data from socket.

    unsigned int counter = 0;//Common counter for all purposes.
    cbuf_tcp::iterator it = cbuf.begin();
	
    // initialize socket connection
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket");

    int n = 1024 * 64;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
    cbuf.reserve(n);
    
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
	 *     An FINACK
	 *  2. TCPD
	 *    a. SEND
	 *    b. TIMEOUT
	 *    c. CLOSE
	 */
	debugf("##### Waiting for a packet ######");
	fromlen = sizeof(fromlen);

	if(closing && cbuf.size() == 0){
	    createAndSendTcpFinPacket(sequence, sock_fin);
	    fin_sent = true;
	}
	
	total_read = recvfrom(sock, &buffer, sizeof(buffer), 0, (sockaddr *)&sock_from, &fromlen);
	
	if(fin_sent)fin_timeout++;
	
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
		printf("CHECKSUM ERROR, ack=%d\n", packet_tcp.ack);
		//drop the packet
		continue;
	    }

	    //ack it
	    it = cbuf.begin();counter = 1;
	    while (it != cbuf.end() && counter <= WINDOW_SIZE){
		if((*it).sequence == packet_tcp.ack){
		    (*it).acked = 1;
		    rtoCalc(gettimeofday_ms() - (*it).timestamp);
		    //printf("RTT=%d RTO=%d", gettimeofday_ms() - (*it).timestamp, rto);
		    break;
		}
		++it;counter++;
	    }

	    //Tell the timer were acked.
	    ackTimer(packet_tcp.ack);
	    
	    //FINACK has been received
	    if(packet_tcp.ack == seq_close +1){
		createAndSendTcpAckPacket(packet_tcp.sequence + 1, sock_fin);
		break;
	    }

	    //slide the window over if possible
	    it = cbuf.begin();
	    while (it != cbuf.end()){
		if((*it).acked == 1){
		    cbuf.pop_front();
		    it = cbuf.begin();
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
	    if(packet_tcpd.type == TYPE_TIMEOUT && fin_timeout <=3){
		debugf("TIMEOUT: PACKET %d", packet_tcpd.sequence);
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
	    if(packet_tcpd.type == TYPE_CLOSE && !closing){
		sequence++;
		seq_close = sequence;
		debugf("CLOSE Connection FIN seq=%d", sequence);
		closing = true;
		memset(&sock_fin, 0, sizeof(sockaddr));
		memcpy(&sock_fin, &packet_tcpd.sock_dest, sizeof(packet_tcpd.sock_dest));
	    }

	    //If where closing we no longer take data to send
	    if(!closing){
		debugf("Contained %d Bytes of Data\n", packet_tcpd.data_len);
	    
		//Place it in the buffer
		if(cbuf.size() == cbuf.capacity()){
		    //if the buffer is full, must force SEND to wait
		    //until it frees up. To do this via implementation we
		    //should set a flag and location to hold the data that
		    //needs to be buffered. When the buffer frees up we
		    //push the data on the end and tell the SEND function
		    //were ready for more.;
		    memset(&packet_tcpd_full, 0, sizeof(tcpd_packet));
		    memcpy(&packet_tcpd_full, &packet_tcpd, sizeof(packet_tcpd));
		    memset(&sock_from_full, 0, sizeof(sockaddr));
		    memcpy(&sock_from_full, &sock_from, sizeof(sock_from));
		    buff_full = true;
		    debugf("BUFFER WAS FULL");
		}else{
		    packet_tcpd.acked = 0;
		    packet_tcpd.sent = 0;
		    packet_tcpd.sequence = sequence++;
		    cbuf.push_back(packet_tcpd);
		
		    //Tell SEND it was succefull.
		    tcpd_packet packet_tcpd_send; //Messages within TCPD
		    memset(&packet_tcpd_send, 0, sizeof(tcpd_packet));
		    packet_tcpd_send.sent = 1;
		    if(sendto(sock, (char *)&packet_tcpd_send, sizeof(tcpd_packet), 0, (struct sockaddr *)&sock_from, sizeof sock_from) < 0)
			err("Error sending SEQ to Timer");
		}//buff full
	    
	    }//!closing
	}//packet type
	
	//send everthing currently not sent
	it = cbuf.begin();counter = 1;
	while (it != cbuf.end() && counter <= WINDOW_SIZE){
	    if((*it).sent == 0){
		createAndSendTcpPacket((*it));
		(*it).sent = 1;
		(*it).timestamp = gettimeofday_ms();
	    }
	    ++it;counter++;
	}
	
	//If the buff opened up, free it.
	if(cbuf.size() < cbuf.capacity() && buff_full == true){
	    tcpd_packet packet_tcpd;
	    memset(&packet_tcpd, 0, sizeof(tcpd_packet));
	    memcpy(&packet_tcpd, &packet_tcpd_full, sizeof(packet_tcpd));

	    packet_tcpd.acked = 0;
	    packet_tcpd.sent = 0;
	    packet_tcpd.sequence = sequence++;
	    cbuf.push_back(packet_tcpd);
	    buff_full = false;
		
	    //Tell SEND it was succefull.
	    tcpd_packet packet_tcpd_send; //Messages within TCPD
	    memset(&packet_tcpd_send, 0, sizeof(tcpd_packet));
	    packet_tcpd_send.sent = 1;
	    if(sendto(sock, (char *)&packet_tcpd_send, sizeof(tcpd_packet), 0, (struct sockaddr *)&sock_from_full, sizeof sock_from_full) < 0)
		err("Error sending SEQ to Timer");
	}

	if(fin_timeout >1)break;
    }
    
    close(sock);
    printf("=========================================\n");
    printf("   TCPDC Connection closed succesfully\n");
    printf("=========================================\n");
}
/*
 * Sends a message via UDP to the timer process
 * telling it to start a timer for sequence with
 * a RTO of timeout.
 */
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

/*
 * Sends a message via UDP to the timer process
 * telling it to remove an acked packet from the
 * timer. Only the sequnce that should be removed
 * is sent.
 */
void ackTimer(uint32_t sequence){
    timer_packet packet_timer; //Message for timer
    memset(&packet_timer, 0, sizeof(timer_packet));
    packet_timer.sequence = sequence;
    packet_timer.ACK = 1;
    if(sendto(sock, (char *)&packet_timer, sizeof(timer_packet), 0, (struct sockaddr *)&sock_timer, sizeof sock_timer) < 0)
	err("Error sending ACK to Timer for SEQ=%d");
    debugf("Sent ACK of %d to timer", packet_timer.sequence);
}

/*
 * Creates a tcp packet, placing data and setting the
 * appropriate flags to have sent to the troll process.
 * The timer is also started here.
 */
void createAndSendTcpPacket(tcpd_packet packet_tcpd){
    
    tcp_packet packet_tcp;

    usleep(1 * 1000);//prevents UDP bffr overflow
    
    //Compile the data into a tcp packet
    memset(&packet_tcp, 0, sizeof(tcp_packet));
    memcpy(&packet_tcp.data, &packet_tcpd.data, sizeof(packet_tcpd.data));
    packet_tcp.data_len = packet_tcpd.data_len;

    //Set some additional TCP fields
    packet_tcp.sequence = packet_tcpd.sequence;
    packet_tcp.source_port = TCPD_CLIENT_PORT;
    packet_tcp.destination_port = packet_tcpd.sock_dest.sin_port;

    //Calculate CRC and place it into the packet
    packet_tcp.checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
    //Ready it for the troll
    NetMessage msg;
    memset(&msg, 0, sizeof(NetMessage));
    memcpy(&msg.msg_header, &packet_tcpd.sock_dest, sizeof(sockaddr_in));
    memcpy(&msg.msg_contents, &packet_tcp, sizeof(tcp_packet));

    startTimer(packet_tcp.sequence, rto);
    
    if(sendto(sock, (char *)&msg, sizeof(NetMessage), 0, (struct sockaddr *)&sock_troll, sizeof sock_troll) < 0)
	err("Error sending to Troll seq=%d", packet_tcp.sequence);

    debugf("SENT seq=%d, port=%d", packet_tcp.sequence, packet_tcp.destination_port);

    packet_tcpd.sent = 1;
}

/*
 * Sends a FIN packet via TCP to the server.
 * This is used for cloasing a connection. This function
 * is similar to sendTCP except for different flags.
 */
void createAndSendTcpFinPacket(unsigned int seq, sockaddr_in destination){
    
    tcp_packet packet_tcp;
    
    //Compile the data into a tcp packet
    memset(&packet_tcp, 0, sizeof(tcp_packet));
    packet_tcp.sequence = seq;
    packet_tcp.FIN = 1;

    //Calculate CRC and place it into the packet
    packet_tcp.checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
    //Ready it for the troll
    NetMessage msg;
    memset(&msg, 0, sizeof(NetMessage));
    memcpy(&msg.msg_header, &destination, sizeof(sockaddr_in));
    memcpy(&msg.msg_contents, &packet_tcp, sizeof(tcp_packet));
    
    if(sendto(sock, (char *)&msg, sizeof(NetMessage), 0, (struct sockaddr *)&sock_troll, sizeof sock_troll) < 0)
	err("Error sending to Troll seq=%d", packet_tcp.sequence);

    startTimer(packet_tcp.sequence, rto);
}

/*
 * Sends a ACK packet to the server. This is used for closing
 * a connection.
 */
void createAndSendTcpAckPacket(unsigned int ack, sockaddr_in destination){
    
    tcp_packet packet_tcp;
    
    //Compile the data into a tcp packet
    memset(&packet_tcp, 0, sizeof(tcp_packet));
    packet_tcp.ack = ack;
    packet_tcp.ACK = 1;

    //Calculate CRC and place it into the packet
    packet_tcp.checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
    //Ready it for the troll
    NetMessage msg;
    memset(&msg, 0, sizeof(NetMessage));
    memcpy(&msg.msg_header, &destination, sizeof(sockaddr_in));
    memcpy(&msg.msg_contents, &packet_tcp, sizeof(tcp_packet));
    
    if(sendto(sock, (char *)&msg, sizeof(NetMessage), 0, (struct sockaddr *)&sock_troll, sizeof sock_troll) < 0)
	err("Error sending to Troll seq=%d", packet_tcp.ack);

    debugf("SENT ACK=%d, port=%d", ack, destination.sin_port);
}


//Used to calculate the RTO
//The most recent RTO calculation is required..
unsigned long rtoCalc(unsigned long rtt){
    double M = rtt / 1000.0;
    double tmp_A = rto_A;
    rto_A = tmp_A + .125 * (double(M) - tmp_A);
    rto_D = rto_D + .25 * ((double(M) - tmp_A) - rto_D);
    //printf("A=%.3f, D=%.3f M=%.3f rtt=%d\n", rto_A, rto_D, M, rtt);
    rto = long((rto_A + 4 * rto_D) * 1000);if(rto > MAXRTO)rto = MAXRTO;
    return rto;
}
