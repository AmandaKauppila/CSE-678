/**
  Timer Proccess
  The timer process keeps track of of packet timeouts. if the
  timeout value within the packet is reached before a corresponding
  ACK is received, the TCPD is notified to resend the packet.
 
  @author Gregory Shayko/Amanda Kauppila
  
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <list>

#include "TCP.h"
#include "utils.h"

using namespace std;

int removeNode(int seq);

typedef struct{
    int dtime; //in us
    int sequence;
    int timeout; //in us
    struct timeval start;
} Node;

list<Node> dlist;
list<Node>::iterator it; //used for looping dlist


/*
 * Implementation
 *  1. Sets up local UDP for incoming packets
 *  2. Check if RECV has packets
 *    a. If the packet is a new timer
 *      i. Add the packet timer to the list
 *    b. If the packet is an ACK
 *      i. Remove the timer from the list
 *  3. Check if any Timeouts occured
 *    a. Alert the TCPD and remove the timeout
 *
 *  Notes:
 *  - The timer process only works if the timeout is the same for every
 *    node. This can be done by doing a selection insert with the current
 *    code from the rear forward.
 */
int main(){
    
    //Change to predefined struct format between TCPD and Timer
    timer_packet packet;

    //time variables
    unsigned int start, end;
    
    // select vars I/O multiplexing
    fd_set rfds;
    struct timeval tv, time_curr;
    int retval;
    
    // socket vars
    int sock;// only need one socket for both sending and receiving
    struct sockaddr_in sock_timer,
                       sock_tcpd;
    /* initialize send socket connection for UDP (DGRAM for UDP,
     * STREAM for TCP) in unix domain */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)err("error opening datagram socket");
    debugf("Socket Connection Complete, sock=%d", sock);

    // setup socket address for local timer
    sock_timer.sin_family = AF_INET;
    sock_timer.sin_port = ntohs(TIMER_PORT);
    sock_timer.sin_addr.s_addr = INADDR_ANY;
    // setup socket address to tcpd
    sock_tcpd.sin_family = AF_INET;
    sock_tcpd.sin_port = htons(TCPD_CLIENT_PORT);
    sock_tcpd.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(sock, (struct sockaddr *)&sock_timer, sizeof(sock_timer)) < 0)
	err("Error binding timer port %d", sock_timer.sin_port);
    debugf("Binded to port %d", sock_timer.sin_port);
    
    //set the timerval for select
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // .1s
    
    while(1){
	// ready select
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	
	// select
	retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);

	//get the time
	gettimeofday(&time_curr, NULL);
	    
	// if recv ready
	if(retval > 0){
	    // read recv
	    memset(&packet, 0, sizeof(timer_packet));// clear the packet
	    if(recvfrom(sock, &packet, sizeof(timer_packet), 0, NULL, NULL) < 0)
		err("Error reading from socket");
	    
	    if(packet.SEQ == 1){
		// add to list
		Node tmp;
		memset(&tmp, 0, sizeof(Node));
		tmp.sequence = packet.sequence;
		tmp.timeout = packet.timeout;
		tmp.start.tv_sec = time_curr.tv_sec;
		tmp.start.tv_usec = time_curr.tv_usec;
		
		//Calculate the time
		int backTimeout = dlist.back().timeout;
		int sec = tmp.start.tv_sec - dlist.back().start.tv_sec;
		int usec = tmp.start.tv_usec - dlist.back().start.tv_usec;
		int time_s = sec * 1000000 + usec;
		tmp.dtime = time_s + tmp.timeout;
		
		dlist.push_back(tmp);
		
	    }else if(packet.ACK == 1){
		// remove from list
		removeNode(packet.sequence);
	    }
	
	}

	//get the time
	gettimeofday(&time_curr, NULL);
	
	// check list for expired timers
	// send to orig port that seq expired.
	if(dlist.size() > 0){
	    it=dlist.begin();
	    int diff = (((Node)*it).start.tv_sec * 1000000 + ((Node)*it).start.tv_usec) +((Node)*it).timeout - (time_curr.tv_sec * 1000000 + time_curr.tv_usec);;
	    while(diff <= 0){
		//ready and send the tcpd packet
		debugf("Timer Expired, seq=%d", ((Node)*it).sequence);
		tcpd_packet packet_tcpd;
		memset(&packet_tcpd, 0, sizeof(tcpd_packet));
		packet_tcpd.type = TIMEOUT;
		packet_tcpd.sequence = ((Node)*it).sequence;
		sendto(sock, &packet_tcpd, sizeof(tcpd_packet), 0, (struct sockaddr *)&sock_tcpd, sizeof(sock_tcpd));
		//remove the timed out timer
		dlist.pop_front();
		if(dlist.size()==0)break;
		it=dlist.begin();
		diff = (((Node)*it).start.tv_sec * 1000000 + ((Node)*it).start.tv_usec) +((Node)*it).timeout - (time_curr.tv_sec * 1000000 + time_curr.tv_usec);
	    }
	}
    }
}

//helper function for the removeNode function.
int sequenceToRemove;
bool isSeqToRemove(const Node& value) { 
    return (value.sequence == sequenceToRemove); 
}

//Removes a node from the dlist given
//a sequence number
int removeNode(int seq){
    sequenceToRemove = seq;
    dlist.remove_if(isSeqToRemove);
}
