/* Primitives for supporting dataflow execution */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>

#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"
#include "msg.h"


/**********************************************************
 Fields and formats

Basic units (all sizes given in bytes)

Agent: 2
Message sequence number: 4
Operation opcode: 1
Message code: 2
Port: 2
IP Address: 4
Word count: 2

Composite quantities.  All listed from MSB to least:

Operator ID: 6.  Agent (2) + Sequence Number (4)
Operand ID:  7.  Operator ID (6) + Offset (1)
Node ID:     6.  Port (2) + IP Address (4)
Agent Map:   8.  Agent (2) + Node ID (6)



**********************************************************/

#define MASK8 ((word_t) 0xFF)
#define MASK16 ((word_t) 0xFFFF)
#define MASK32 ((word_t) 0xFFFFFFFF)

/** Constructors **/

/* Create an operand destination */
word_t msg_build_destination(unsigned agent, unsigned operator_id,
			     unsigned offset) {
    return
	((word_t) (agent & MASK16) << 48) |
	((word_t) (operator_id & MASK32) << 16) |
	((offset & MASK8) << 8);
}


/* Create IP address from port and host */
word_t msg_build_node_id(unsigned port, unsigned ip) {
    return
	((word_t) (port & MASK16) << 32) |
	ip;
}


/** Extractors **/
bool msg_is_client_agent(unsigned agent) {
    return agent >= (1<<15);
}

unsigned msg_get_header_code(word_t header) {
    return (unsigned) (header & MASK8);
}

/* Agent from upper 16 bits */
unsigned msg_get_header_agent(word_t header) {
    return (unsigned) (header >> 48) & MASK16;
}

unsigned msg_get_header_op_id(word_t header) {
    return (unsigned) (header >> 16) & MASK32;    
}

unsigned msg_get_header_opcode(word_t header) {
    return (unsigned) (header >> 8) & MASK8;    
}

unsigned msg_get_header_offset(word_t header) {
    return (unsigned) (header >> 8) & MASK8;
}

unsigned msg_get_header_port(word_t header) {
    return (unsigned) (header >> 48) & MASK16;    
}
unsigned msg_get_header_ip(word_t header) {
    return (unsigned) (header >> 16) & MASK32;    
}

unsigned msg_get_header_wordcount(word_t header) {
    return (unsigned) (header >> 32) & MASK16;    
}

unsigned msg_get_header_workercount(word_t header) {
    return (unsigned) (header >> 16) & MASK16;        
}

unsigned msg_get_header_snb(word_t header) {
    return (unsigned) (header >> 8) & MASK8;
}

unsigned msg_get_header_generation(word_t header) {
    return (unsigned) (header >> 8) & MASK32;
}

/** Message builders **/

/* Create an empty operator */
/* len specifies total message length, including header */
chunk_ptr msg_new_operator(unsigned opcode, unsigned agent, unsigned operator_id,
			   unsigned len) {
    if (len > OP_MAX_LENGTH) {
	err(true, "Requested operator length %u > max allowable %u",
	    len, (unsigned) OP_MAX_LENGTH);
	return false;
    }
    chunk_ptr result = chunk_new(len);
    word_t h1 = ((word_t) agent << 48) | ((word_t) operator_id) << 16 |
	(opcode << 8) | MSG_OPERATION;
    chunk_insert_word(result, h1, 0);
    /* Add valid bits (Initially only header and valid mask) */
    word_t vmask = 0x3u;
    chunk_insert_word(result, vmask, 1);
    return result;
}

/* Create destination from operator */
word_t msg_new_destination(chunk_ptr operator, unsigned offset) {
    word_t h = chunk_get_word(operator, 0);
    /* Replace opcode and code of header */
    h = (h & ~MASK16) | (offset << 8);
    return h;
}

/* Extracting information from destination */
unsigned msg_get_dest_agent(word_t dest) {
    return msg_get_header_agent(dest);
}

unsigned msg_get_dest_op_id(word_t dest) {
    return msg_get_header_op_id(dest);
}
unsigned msg_get_dest_offset(word_t dest) {
    return (dest >> 8) & MASK8;
}


/* Create an empty operand.  len specifies total message size, including header */
chunk_ptr msg_new_operand(word_t dest, unsigned len) {
    chunk_ptr result = chunk_new(len);
    word_t h1 = dest | MSG_OPERAND;
    chunk_insert_word(result, h1, 0);
    return result;
}

chunk_ptr msg_new_register_router(unsigned port) {
    chunk_ptr result = chunk_new(1);
    word_t h1 = ((word_t) port << 48) | MSG_REGISTER_ROUTER;
    chunk_insert_word(result, h1, 0);
    return result;
}

/* Message consisting only of code */
static chunk_ptr msg_new_op(unsigned code) {
    chunk_ptr result = chunk_new(1);
    word_t h1 = code;
    chunk_insert_word(result, h1, 0);
    return result;
}

/* Create message to register client, router, or worker */
chunk_ptr msg_new_register_client() {
    return msg_new_op(MSG_REGISTER_CLIENT);
}

chunk_ptr msg_new_register_worker() {
    return msg_new_op(MSG_REGISTER_WORKER);
}

chunk_ptr msg_new_register_agent(unsigned agent) {
    chunk_ptr result = chunk_new(1);
    word_t h1 = ((word_t) agent << 48) | MSG_REGISTER_AGENT;
    chunk_insert_word(result, h1, 0);
    return result;
}

chunk_ptr msg_new_worker_ready(unsigned agent) {
    chunk_ptr result = chunk_new(1);
    word_t h1 = ((word_t) agent << 48) | MSG_READY_WORKER;
    chunk_insert_word(result, h1, 0);
    return result;
}

chunk_ptr msg_new_nack() {
    return msg_new_op(MSG_NACK);
}

chunk_ptr msg_new_kill() {
    return msg_new_op(MSG_KILL);
}

/*
  Create a message containing worker statistics.
  Specify number of values and provide pointer to array of them.
 */
chunk_ptr msg_new_stat(unsigned nworker, unsigned nstat, size_t *vals) {
    chunk_ptr msg = chunk_new(nstat+1);
    word_t h = ((nworker & MASK16) << 16) | MSG_STAT;
    chunk_insert_word(msg, h, 0);
    unsigned i;
    for (i = 0; i < nstat; i++)
	chunk_insert_word(msg, vals[i], i+1);
    return msg;
}

chunk_ptr msg_new_flush() {
    return msg_new_op(MSG_DO_FLUSH);
}

/* Create message containing global operation data */
/* nwords specifies number of data words (not including header) */
chunk_ptr msg_new_cliop_data(unsigned agent, unsigned opcode, unsigned nword,
			     word_t *data) {
    chunk_ptr msg = chunk_new(nword+1);
    word_t h = ((word_t) agent << 48) | opcode << 8 | MSG_CLIOP_DATA;
    chunk_insert_word(msg, h, 0);
    unsigned i;
    for (i = 0; i < nword; i++)
	chunk_insert_word(msg, data[i], i+1);
    return msg;
}

chunk_ptr msg_new_cliop_ack(unsigned agent) {
    chunk_ptr msg = chunk_new(1);
    word_t h = ((word_t) agent << 48) | MSG_CLIOP_ACK;
    chunk_insert_word(msg, h, 0);
    return msg;
}

chunk_ptr msg_new_gc_request(unsigned gen) {
    chunk_ptr msg = chunk_new(1);
    word_t h = ((word_t) gen << 8) | MSG_GC_REQUEST;
    chunk_insert_word(msg, h, 0);
    return msg;
}

chunk_ptr msg_new_gc_start() {
    return msg_new_op(MSG_GC_START);
}

chunk_ptr msg_new_gc_finish() {
    return msg_new_op(MSG_GC_FINISH);
}

/* Create listening socket.
   Port value of 0 indicates that port can be chosen arbitrarily.
   If successful, set fdp to fd for listening socket and portp to port.
 */

bool new_server(unsigned port, int *fdp, unsigned *portp) {
    int listenfd;
    int optval = 1;
    struct sockaddr_in addr;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	err(false, "Couldn't create listening socket");
	return false;
    }
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		   (const void *) &optval, sizeof(int)) < 0) {
	err(false, "Couldn't set socket option");
	return false;
    }
    bzero((char *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (port > 0) {
	addr.sin_port = htons((unsigned short) port);
	if (bind(listenfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
	    err(false, "Couldn't bind to port %u", port);
	    return false;
	}
    } else {
	int ntries = 5;
	bool done = false;
	/* Need to get make sure different nodes don't attempt
	   the same random sequence */
	srandom(getpid());
	while (!done && ntries > 0) {
	    port = 5000 + random() % 5000;
	    addr.sin_port = htons((unsigned short) port);
	    done = bind(listenfd, (struct sockaddr *) &addr, sizeof(addr)) >= 0;
	    report(4, "Tried opening server on port %u: %s",
		   port, done ? "OK" : "Failed");
	    ntries--;
	}
	if (!done) {
	    err(false, "Failed to set up server");
	    return false;
	}
    }
    if (listen(listenfd, 1024) < 0) {
	err(false, "Couldn't set socket to listen");
	return false;
    }
    if (fdp)
	*fdp = listenfd;
    if (portp)
	*portp = port;
    return true;
}



/* Open connection to server.  Return socket file descriptor */
int open_clientfd(char *hostname, unsigned port) {
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return -1; /* check errno for cause of error */

    /* Fill in the server's IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL)
	return -2; /* check h_errno for cause of error */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
	return -1;
    return clientfd;
}

/* Open connection to server based on IPv4 addrss.
   Return socket file descriptor */
int open_clientfd_ip(unsigned ip, unsigned port) {
    int clientfd;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return -1; /* check errno for cause of error */

    /* Fill in the server's IP address and port */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    unsigned nip = htonl(ip);
    bcopy((char *) &nip, 
	  (char *)&serveraddr.sin_addr.s_addr, 4);
    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
	return -1;
    return clientfd;
}


/* Accept a connection request from a client
   Return connection socket descriptor.
   (Optionally) update pointers to IP address */
int accept_connection(int listenfd, unsigned *ipp) {
    struct sockaddr_in clientaddr;
    struct hostent *hp;
    char *haddrp;
    socklen_t len = sizeof(clientaddr);
    int clientfd = accept(listenfd, (struct sockaddr *) &clientaddr, &len);
    if (clientfd < 0) {
	err(false, "Accept failed");
	return clientfd;
    }
    hp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
		       sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    haddrp = inet_ntoa(clientaddr.sin_addr);
    if (hp) {
	report(3, "Accepted connection from %s (%s)", hp->h_name, haddrp);
    } else {
	err(false, "Could not get host name for host %s", haddrp);
    }
    if (ipp) {
	*ipp = ntohl(clientaddr.sin_addr.s_addr);
    }
    return clientfd;
}
