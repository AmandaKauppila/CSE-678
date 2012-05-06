
#ifndef TROLL_H
#define TROLL_H

#define MAXNETMESSAGE 1500

typedef struct NetMessage {
	struct sockaddr_in msg_header;
	char msg_contents[MAXNETMESSAGE - (sizeof (struct sockaddr_in))];
} NetMessage;

#endif

