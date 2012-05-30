/*
  File Transfer Protocal Daemon - Server Side
  The daemon mimics the TCP functionality of the OS using
  the UDP protocal available.
  
  Implementation
   1. Opens 2 sockets, one for local communication, other
      for external communication.
   2. Binds the local soacket to a static application port
   3. Waits for a RECV call from the binded port.
   4. Binds the external port to the provided port via the
      RECV function
   5. Once data is received form that port, verify the
      checksum and sequence
   6. Send the data to the Client Application.
  
   Notes:
   - N/A

  @author Gregory Shayko
*/
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> //gethostbyname

#include "TCP.h"
#include "troll.h"
#include "utils.h"
#include "checksum.h"
#include "circular.h"

typedef circular_buffer<tcpd_packet> cbuf_tcp;
cbuf_tcp cbuf(64);

void printBuffer(){
    cbuf_tcp::iterator it = cbuf.begin();
    printf("[Buffer] %d [", cbuf.size());
    while (it != cbuf.end()){
	printf("%d-%d,",(*it).sequence,(*it).acked);
	it++;
    }
    printf("]\n");
}


void sendAck(uint32_t ack, unsigned short port, sockaddr_in destination);

int sock; //socket descriptor for TCPD
int sock_ext; //socket used for incoming network connections
struct sockaddr_in sock_tcpds;/* structure for socket name setup */
struct sockaddr_in sock_recv;
struct sockaddr_in sock_tcp;
struct sockaddr_in sock_troll;
struct sockaddr_in sock_from;
socklen_t sock_recv_len;
socklen_t fromlen;
struct hostent *recv_host;

int main(int argc, char* argv[]){
    
    tcpd_packet packet_tcpd;
    tcp_packet packet_tcp;

    cbuf_tcp::iterator it = cbuf.begin();
    
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket");
    if((sock_ext = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket ext");

    debugf("Sockets Opened local=%d, external=%d", sock, sock_ext);
    
    /* Set up the address to bind tcpd */
    sock_tcpds.sin_family = AF_INET;
    sock_tcpds.sin_addr.s_addr = INADDR_ANY;
    sock_tcpds.sin_port = htons(TCPD_SERVER_PORT);
    
    sock_troll.sin_family = AF_INET;
    sock_tcp.sin_family = AF_INET;
    
    /* bind TCPD Server to Local Port */
    if(bind(sock, (struct sockaddr *)&sock_tcpds, sizeof(sock_tcpds)) < 0)
	err("Error binding socket");

    debugf("TCPD locally binded to port %d", sock_tcpds.sin_port);

    
    /* RECV Setup */
    if ((recv_host = gethostbyname("localhost")) == NULL)
	err("Unknown recv host 'localhost'\n");

    unsigned short curr_port = 0;
    unsigned int low_seq = 0;

    int total_read = 0;
    unsigned int counter;
    
    //Wait for bind
    debugf("Waiting for BIND from application");
	
    memset(&packet_tcpd, 0, sizeof(tcpd_packet));
    memset(&packet_tcp, 0, sizeof(tcp_packet));
	
    bzero((char *)&sock_recv, sizeof(sockaddr_in));
    sock_recv_len = sizeof(sock_recv);
    if(recvfrom(sock, &packet_tcpd, sizeof(packet_tcpd), 0, (struct sockaddr *)&sock_recv, &sock_recv_len) < 0)
	err("Invalid receive from BIND");

    //Ready for recv
    sock_recv.sin_port = TCPD_RECV_PORT;
    curr_port = packet_tcpd.port;
    
    /* bind TCPD Server to Local Port */
    if(bind(sock_ext, (struct sockaddr *)&packet_tcpd.sock_dest, sizeof(sockaddr)) < 0)
	err("Error binding socket for tcp port=%d",sock_tcp.sin_port);
    
    debugf("BIND on port=%d", packet_tcpd.sock_dest.sin_port);
    
    // Main Application loop
    // This loop always runs so tcpds never has be restarted
    while(1){
    	
    	/*
	 * 1. TCP LOOP (loops until connection closed via CLOSE)
	 *    a. Wait for tcp_packet. First packet is the ISN (initial sequence #)
	 *    b. Check CRC checksum
	 *    c. If tcp_packet.DATA
	 *        i. Seqeunce check
	 *       ii. Send ACK
	 *      iii. Return to STEP(2a)
	 &    d. If tcp_packet.CLOSE
	 *        i. close logic
	 *       ii. exit loop, goto STEP(3)
	 *    e. Send data to client application
	 * 3. Unbind port to socket
	 * 4. Cleanup used resources
	 * 5. Return to STEP(1) waiting for another TCP connection
	 */
	debugf("----Waiting for TCP Packet----");
	
	NetMessage msg;
	memset(&msg, 0, sizeof(NetMessage));
	fromlen = sizeof(fromlen);
	total_read = recvfrom(sock_ext, &msg, sizeof(NetMessage), 0,(sockaddr *)&sock_from, &fromlen); 

	memset(&packet_tcp, 0, sizeof(tcp_packet));
	memcpy(&packet_tcp, &msg.msg_contents, sizeof(tcp_packet));

	//Check the CHECKSUM, drop if wrong.
	int checksum_in = packet_tcp.checksum;
	packet_tcp.checksum = 0;
	int checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
	//debugf("Checksum = %d vs Calculated = %d", checksum_in, checksum);

	if(checksum != checksum_in){
	    printf("CHECKSUM ERROR, seq=%d", packet_tcp.sequence);
	    continue;
	}

	//If the buffer is full, drop it.
	if(cbuf.size() == cbuf.capacity()){
	    printf("BUFFER FULL, seq=%d", packet_tcp.sequence);
	    continue;
	}

	// IF ITS > WINDOW, DROP IT
	if(packet_tcp.sequence > low_seq + WINDOW_SIZE){
	    printf("GRTR WINDOW, seq=%d", packet_tcp.sequence);
	    continue;
	}
	
	//TODO
	//CHECK THE SEQUENCE NUMBER AGAINST WINDOW BUFFER
	// ELSE Place it in the window in its spot.
	debugf("Sequence = %d, lowSeq=%d", packet_tcp.sequence, low_seq);
	it = cbuf.begin();counter = low_seq;
	//it != cbuf.end() &&
	while (counter <= low_seq + WINDOW_SIZE && packet_tcp.sequence >= low_seq){
	    
	    if(it == cbuf.end()){
		//debugf("Sequence not found at end. Create seq=%d",counter);
		//create an empty tcpd packet and push it to the back
		tcpd_packet tmp_tcpd;
		memset(&tmp_tcpd, 0, sizeof(tcpd_packet));
		tmp_tcpd.sequence = counter;
		tmp_tcpd.acked = 0;
		cbuf.push_back(tmp_tcpd);
	    }
	    
	    if((*it).sequence == packet_tcp.sequence){
		//debugf("Sequence found at end. Create seq=%d",packet_tcp.sequence);
		if((*it).acked == 0){
		    memcpy(&((*it).data), &packet_tcp.data, sizeof(packet_tcp.data));
		    (*it).data_len = packet_tcp.data_len;
		    (*it).sequence = packet_tcp.sequence;
		    (*it).acked = 1;
		    //debugf("Packet %d was acked to %d.", (*it).sequence,(*it).acked);
		}
		break;
	    }
	    ++it;counter++;
	}

//	debugf("buffer size=%d", cbuf.size());
	printBuffer();

	//Send the ACK 
	//Always send a packet since were assured this is <= max
	//sequence for the window
	sock_tcp.sin_addr.s_addr = sock_from.sin_addr.s_addr;
	sock_tcp.sin_port = htons(sock_from.sin_port);
	sendAck(packet_tcp.sequence, packet_tcp.source_port, sock_tcp);
	
	//Send all received packets to the recv in the front of the buffer.
	it = cbuf.begin();
	while (it != cbuf.end()){
	    //debugf("CHECK BUFF seq=%d acked=%d", (*it).sequence, (*it).acked);
	    if((*it).acked == 1){
		packet_tcpd = (*it);
		
		usleep(1 * 1000);//prevents UDP bffr overflow
		if(sendto(sock, &packet_tcpd, sizeof(tcpd_packet), 0, (struct sockaddr *)&sock_recv, sock_recv_len) < 0)
		    err("Error sending to recv");
		debugf("Sent %d bytes to RECV(%d)", packet_tcpd.data_len, sock_recv.sin_port);
		cbuf.pop_front();
		low_seq++;
		it = cbuf.begin();
	    }else{
		break;
	    }
	}
	
    }
    close(sock);
    close(sock_ext);
}

void sendAck(uint32_t ack, unsigned short port, sockaddr_in destination){
    
    usleep(1 * 1000);//prevents UDP bffr overflow
    
    //Compile the data into a tcp packet
    tcp_packet packet_tcp;
    memset(&packet_tcp, 0, sizeof(tcp_packet));

    //Set some additional TCP fields
    packet_tcp.ack = ack;
    packet_tcp.ACK = 1;

    //Calculate CRC and place it into the packet
    packet_tcp.checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
    
    destination.sin_port = htons(port);
    
    //Ready it for the troll
    NetMessage msg;
    memset(&msg, 0, sizeof(NetMessage));
    memcpy(&msg.msg_header, &destination, sizeof(sockaddr_in));
    memcpy(&msg.msg_contents, &packet_tcp, sizeof(tcp_packet));
    
    if(sendto(sock_ext, (char *)&msg, sizeof(NetMessage), 0, (struct sockaddr *)&sock_from, fromlen) < 0)
	err("Error sending to Troll ack=%d", ack);
    
    debugf("SENT ACK==%d, port=%d", ack, port);
}
