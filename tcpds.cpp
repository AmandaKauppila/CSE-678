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

int main(int argc, char* argv[]){
    
    
    int sock; //socket descriptor for TCPD
    int sock_ext; //socket used for incoming network connections
    struct sockaddr_in sock_tcpds;/* structure for socket name setup */
    struct sockaddr_in sock_recv;
    socklen_t sock_recv_len = sizeof(struct sockaddr_in);
    struct hostent *recv_host;

    tcpd_packet packet_tcpd;
    tcp_packet packet_tcp;
    
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket");
    if((sock_ext = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	err("Error openting datagram socket ext");

    debugf("Sockets Opened local=%d, external=%d", sock, sock_ext);
    
    /* Set up the address to bind tcpd */
    sock_tcpds.sin_family = AF_INET;
    sock_tcpds.sin_addr.s_addr = INADDR_ANY;
    sock_tcpds.sin_port = htons(TCPD_SERVER_PORT);

    /* bind TCPD Server to Local Port */
    if(bind(sock, (struct sockaddr *)&sock_tcpds, sizeof(sock_tcpds)) < 0)
	err("Error binding socket");

    debugf("TCPD locally binded to port %d", sock_tcpds.sin_port);
    
    /* RECV Setup */
    if ((recv_host = gethostbyname("localhost")) == NULL)
	err("Unknown recv host 'localhost'\n");

    /*
      bzero((char *)&sock_recv, sizeof(sockaddr_in));
      sock_recv.sin_family = AF_INET;
      bcopy(recv_host->h_addr, (char*)&sock_recv.sin_addr, recv_host->h_length);
      sock_recv.sin_port = htons(RECV_PORT);
    */

    int curr_port = 0;
    
    while(1){
	
	int total_read = 0;
	//Wait for recv to say they want a packet
	debugf("Waiting for RECV from application");
	
	memset(&packet_tcpd, 0, sizeof(tcpd_packet));
	memset(&packet_tcp, 0, sizeof(tcp_packet));
	
	bzero((char *)&sock_recv, sizeof(sockaddr_in));
	if(recvfrom(sock, &packet_tcpd, sizeof(packet_tcpd), 0, (struct sockaddr *)&sock_recv, &sock_recv_len) < 0)
	    err("Invalid receive from RECV");

	debugf("RECV on Port %d", packet_tcpd.sock_dest.sin_port);

	/* bind TCPD Server to External Port */
	if(packet_tcpd.sock_dest.sin_port != curr_port){
	    if(bind(sock_ext, (struct sockaddr *)&packet_tcpd.sock_dest, sizeof(sockaddr_in)) < 0)
		err("Error binding socket");
	    curr_port = packet_tcpd.sock_dest.sin_port;
	}
	NetMessage msg;
	memset(&msg, 0, sizeof(NetMessage));
	total_read = recv(sock_ext, &msg, sizeof(NetMessage), 0);

	memset(&packet_tcp, 0, sizeof(tcp_packet));
	memcpy(&packet_tcp, &msg.msg_contents, sizeof(tcp_packet));

	//Check the CHECKSUM
	int checksum_in = packet_tcp.checksum;
	packet_tcp.checksum = 0;
	int checksum = crc16((char *)&packet_tcp, sizeof(tcp_packet), 0);
	
	debugf("Checksum = %d vs Calculated = %d", checksum_in, checksum);
	
	if(checksum != checksum_in){
	    printf("CHECKSUM ERROR, Seq=%d\n", packet_tcp.sequence);
	}

	//CHECK THE SEQUENCE NUMBER AGAINST WINDOW BUFFER
	debugf("Sequence = %d", packet_tcp.sequence);

	//Ready the TCPD packet to RECV
	memset(&packet_tcpd, 0, sizeof(tcpd_packet));
	memcpy(&packet_tcpd.data, &packet_tcp.data, sizeof(packet_tcp.data));
	packet_tcpd.data_len = packet_tcp.data_len;
	
	//Send it to recv.
	if(sendto(sock, &packet_tcpd, sizeof(tcpd_packet), 0, (struct sockaddr *)&sock_recv, sizeof(sock_recv)) < 0)
	    err("Error sending to recv");
	debugf("Sent %d bytes to RECV(%d)", packet_tcpd.data_len, sock_recv.sin_port);
	
    }
    close(sock);
    close(sock_ext);
}
