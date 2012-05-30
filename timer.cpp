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
#include <iostream>

#include "TCP.h"
#include "utils.h"
#include "timeval.h"

using namespace std;

typedef struct{
    unsigned long delta; //ms
    int sequence;
    unsigned long timeout; //ms
    unsigned long timeout_time; //ms
    unsigned short port;
} Node;

list<Node> dlist;
list<Node>::iterator it; //used for looping dlist

int removeNode(int seq, short port);
void addNode(Node node);

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
    unsigned long time_start;
    long time_diff, time_delta;
    
    // select vars I/O multiplexing
    fd_set rfds;
    struct timeval tv, time_curr;
    int retval;

    Node tmp;//used for short instanced of Node
    
    // socket vars
    int sock;// only need one socket for both sending and receiving
    struct sockaddr_in sock_timer,
	               sock_destination,
	               sock_from;
    socklen_t fromlen;
    /* initialize send socket connection for UDP (DGRAM for UDP,
     * STREAM for TCP) in unix domain */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)err("error opening datagram socket");
    debugf("Socket Connection Complete, sock=%d", sock);

    // setup socket address for local timer
    sock_timer.sin_family = AF_INET;
    sock_timer.sin_port = ntohs(TIMER_PORT);
    sock_timer.sin_addr.s_addr = INADDR_ANY;
    
    sock_destination.sin_family = AF_INET;
    sock_destination.sin_addr.s_addr = INADDR_ANY;
    
    fromlen = sizeof(sock_from);
    
    if(bind(sock, (struct sockaddr *)&sock_timer, sizeof(sock_timer)) < 0)
	err("Error binding timer port %d", sock_timer.sin_port);
    debugf("Binded to port %d", sock_timer.sin_port);
    
    while(1){
        //set the timerval for select
	tv.tv_sec = 0;
	tv.tv_usec = 100000; // .1s
	
	// ready select
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	
	time_start = gettimeofday_ms();
	// select
	retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
	
	// if recv ready
	if(retval > 0){
	    // read recv
	    memset(&packet, 0, sizeof(timer_packet));// clear the packet	    
	    if(recvfrom(sock, &packet, sizeof(timer_packet), 0, (struct sockaddr *)&sock_from, &fromlen) < 0)
		err("Error reading from socket");
	    
	    if(packet.SEQ == 1){
		// add to list
		memset(&tmp, 0, sizeof(Node));
		tmp.sequence = packet.sequence;
		tmp.timeout = packet.timeout;
		tmp.timeout_time = gettimeofday_ms() + tmp.timeout;
		tmp.port = sock_from.sin_port;
		addNode(tmp);
		
	    }else if(packet.ACK == 1){
		// remove from list
		debugf("ACKED seq=%d port=%d", packet.sequence, sock_from.sin_port);
		removeNode(packet.sequence, sock_from.sin_port);
	    }
	
	}
	
	// check list for expired timers
	// send to orig port that seq expired.
	Node it_node;
	if(dlist.size() > 0){
	    it=dlist.begin();
	    it_node = (Node)*it;
	    //get the time
	    time_delta = gettimeofday_ms() - (it_node.timeout_time - it_node.timeout);
	    time_diff = it_node.delta - time_delta;
	    //debugf("Checking time_diff=%d", time_diff);
	    while(time_diff <= 0){
		usleep(1 * 1000);//Prevents buffer overflow.
		//ready and send the tcpd packet
		debugf("TIMEOUT seq=%d port=%d", it_node.sequence, it_node.port);
		tcpd_packet packet_tcpd;
		memset(&packet_tcpd, 0, sizeof(tcpd_packet));
		packet_tcpd.type = TYPE_TIMEOUT;
		packet_tcpd.sequence = it_node.sequence;
		// setup socket address to destination
		sock_destination.sin_port = htons(it_node.port);
		if(sendto(sock, &packet_tcpd, sizeof(tcpd_packet), 0, (struct sockaddr *)&sock_destination, sizeof(sock_destination)) < 0)
		    err("Error sending timeout for seq=%d", it_node.sequence);
		time_delta = time_delta - it_node.delta;
		dlist.pop_front();//remove the timed out timer
		
		//Check the next head
		if(dlist.size()==0)break;
		it=dlist.begin();
		it_node = (Node)*it;
		time_diff = it_node.delta - time_delta;
	    }
	    if(time_diff > 0)it_node.delta = time_diff;//if time is left over
	}
    }
}

//Removes a node from the dlist given
//a sequence number
int removeNode(int seq, short port){

    //move the time off this item to the next on the list.
    for (it=dlist.begin() ; it != dlist.end(); it++ ){
	if((*it).sequence == seq && (*it).port == port){
	    unsigned long tmp_delta = (*it).delta;
	    it++;
	    if(it != dlist.end()){
		debugf("Ading delta of %d to seq=%d to=%d##################", tmp_delta, (*it).sequence, (*it).timeout_time);
		(*it).delta = (*it).delta + tmp_delta;
		(*it).timeout_time = (*it).timeout_time + tmp_delta;
	    }
	    it--;
	    dlist.erase(it);
	    break;
	}
    }
}

void addNode(Node node){
    Node it_node;
    timeval time_diff;

    for(it=dlist.end() ; it != dlist.begin(); it--){
	--it;
	it_node = (Node)*it;
	if(node.timeout_time >= it_node.timeout_time){
	    //Calculate the time from n-1 to n
	    node.delta = node.timeout_time - it_node.timeout_time;
	    
	    ++it;
	    dlist.insert(it, node);
	    break;
	}
	++it;
    }

    //Exception if this is the lowest timeout(head of list)
    if(it==dlist.begin()){
	node.delta = node.timeout;
	dlist.push_front(node);
    }
    debugf("ADDED seq=%d port=%d delta=%d timeout=%d timeout_time=%d", node.sequence, node.port, node.delta, node.timeout, node.timeout_time);
}
