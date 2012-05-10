#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> //gethostbyname

#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <list>

#include "TCP.h"
#include "utils.h"

using namespace std;

int nodeAdd(int time, int seq, int port);
int nodeAcked(int seq);

typedef struct{
    int dtime; //in ms
    int sequence;
    int timeout; //in ms
    struct timeval start;
} Node;

list<Node> dlist;
list<Node>::iterator it; //used for looping dlist

int main(){
    
    //Change to predefined struct format between TCPD and Timer
    timer_packet packet;

    //time variables
    unsigned int start, end;
    
    // select vars I/O multiplexing
    fd_set rfds;
    struct timeval tv, time_start;
    int retval;
    
    // socket vars
    int sock;// only need one socket for both sending and receiving
    struct sockaddr_in sock_timer;
    
    /* initialize send socket connection for UDP (DGRAM for UDP,
     * STREAM for TCP) in unix domain */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)err("error opening datagram socket");
    debugf("Socket Connection Complete, sock=%d", sock);
    
    sock_timer.sin_family = AF_INET;
    sock_timer.sin_port = ntohs(TIMER_PORT);
    sock_timer.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(sock, (struct sockaddr *)&sock_timer, sizeof(sock_timer)) < 0)
	err("Error binding timer port %d", sock_timer.sin_port);
    debugf("Binded to port %d", sock_timer.sin_port);
    
    //set the timerval for select
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // .01s
    
    while(1){
	// ready select
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	
	// select
	retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);

	//get the time
	gettimeofday(&time_start, NULL);
	    
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
		tmp.start.tv_sec = time_start.tv_sec;
		tmp.start.tv_usec = time_start.tv_usec;
		
		//Calculate the time
		int backTimeout = dlist.back().timeout;
		int sec = tmp.start.tv_sec - dlist.back().start.tv_sec;
		int usec = tmp.start.tv_usec - dlist.back().start.tv_usec;
		int time_s = sec * 1000000 + usec;
		tmp.dtime = time_s + tmp.timeout;
		
		dlist.push_back(tmp);
	    }else if(packet.ACK == 1){
		// remove from list
		nodeAcked(packet.sequence);
	    }
	    
	    for(it=dlist.begin();it !=dlist.end();it++){
		printf("size=%d\n", (int) dlist.size());
		printf("seq=%d,time=%d\n", ((Node)*it).sequence, ((Node)*it).dtime);
	    }
	
	}
	
	// check list for expired timers
	// send to orig port that seq expired.
	
	
	
	/* check the current list and decide which node is expired */
	/* don't iterate over the list- the first element will always
	 * expire first */
	/*
	  for(list<Node>::iterator it; it = dlist.begin(); it != dlist.end(){
	  //figure out how to calculate the time
	  time_t = clock();
	  int diff = time_t - (*it).time;
	  if(diff <= 0){
	  // timer has expired, send notice to tcpd_client 
	  // format tcp header and packet for buf
	  if(sendto(sockSend, buf, 1024) < 0){
	  printf("error writing to socket\n");
	  exit(1);
	  }
	  }
	  }
	*/
	
	
	
    }
}

int sequenceToRemove;
bool isSeqToRemove(const Node& value) { 
    return (value.sequence == sequenceToRemove); 
}
    
/* calculate time left before adding to list */
/* need to figure out what is needed to store */
/*int nodeAdd(int time, int seq, int port){
  Node node;
  node.time = time;
  node.port = port;
  node.sec_left;
  // node.seq = seq 
  int size = dlist.size();
  // get time of last node 
  int timeLastNode = dlist
  dlist.push_back(node);
  int size2 = dlist.size();
  if (size == size2){
  //  could not add node to list 
  }
  }
*/

int nodeAcked(int seq){
    sequenceToRemove = seq;
    dlist.remove_if(isSeqToRemove);
    // remove from list
    //for(it=dlist.begin();it !=dlist.end();it++){
    //	if(((Node)*it).sequence == packet.sequence){
    //    sequenceToRemove = packet
    //}
    //printf("REMOVED ACK=%d\n", packet.sequence);
    //}
}
