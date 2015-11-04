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
//#include <netinet/in.h>
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
dword_t msg_build_destination(unsigned agent, word_t operator_id,
			      unsigned offset) {
    dword_t result;
    result.w0 = 
	((word_t) (agent & MASK16) << 48) |
	((offset & MASK8) << 8);
    result.w1 = operator_id;
    return result;
}


/* Create IP address from port and host */
word_t msg_build_node_id(unsigned port, unsigned ip) {
    return
	((word_t) (port & MASK16) << 32) |
	ip;
}


/** Single-word header extractors **/
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

/** Double-word header extractors **/

unsigned msg_get_dheader_code(dword_t header) {
    return msg_get_header_code(header.w0);
}

/* Agent from upper 16 bits */
unsigned msg_get_dheader_agent(dword_t header) {
    return msg_get_header_agent(header.w0);
}

word_t msg_get_dheader_op_id(dword_t header) {
    return header.w1;
}

unsigned msg_get_dheader_opcode(dword_t header) {
    return msg_get_header_opcode(header.w0);
}

unsigned msg_get_dheader_offset(dword_t header) {
    return msg_get_header_offset(header.w0);
}


/** Message builders **/

/* Create an empty operator */
/* len specifies total message length, including header */
chunk_ptr msg_new_operator(unsigned opcode, unsigned agent, word_t operator_id,
			   unsigned len) {
    if (len > OP_MAX_LENGTH) {
	err(true, "Requested operator length %u > max allowable %u",
	    len, (unsigned) OP_MAX_LENGTH);
	return false;
    }
    chunk_ptr result = chunk_new(len);
    word_t h0 = ((word_t) agent << 48) | (opcode << 8) | MSG_OPERATION;
    chunk_insert_word(result, h0, 0);
    chunk_insert_word(result, operator_id, 1);
    /* Add valid bits (Initially only header and valid mask) */
    word_t vmask = 0x7u;
    chunk_insert_word(result, vmask, 2);
    return result;
}

/* Create destination from operator */
dword_t msg_new_destination(chunk_ptr operator, unsigned offset) {
    dword_t dh = chunk_get_dword(operator, 0);
    /* Replace opcode and code of header */
    dh.w0 = (dh.w0 & ~MASK16) | (offset << 8);
    return dh;
}

/* Extracting information from destination */
unsigned msg_get_dest_agent(dword_t dest) {
    return msg_get_dheader_agent(dest);
}

word_t msg_get_dest_op_id(dword_t dest) {
    return msg_get_dheader_op_id(dest);
}
unsigned msg_get_dest_offset(dword_t dest) {
    return msg_get_dheader_offset(dest);
}


/* Create an empty operand.  len specifies total message size, including header */
chunk_ptr msg_new_operand(dword_t dest, unsigned len) {
    chunk_ptr result = chunk_new(len);
    dest.w0 |= MSG_OPERAND;
    chunk_insert_dword(result, dest, 0);
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

#define MAXTRIES 5

bool new_server(unsigned port, int *fdp, unsigned *portp) {
    int listenfd;
    int optval = 1;
    struct addrinfo hints, *listp, *p;
    unsigned ports[MAXTRIES];
    char sport[10];
    char ntries;
    int i;

    if (port == 0) {
	/* Generate multiple choices for ports */
	/* Make sure different nodes don't attempt the same random sequence */
	srandom(getpid());
	for (i = 0; i < MAXTRIES; i++) {
	    ports[i] = MINPORT + random() % PORTCOUNT;
	}
	ntries = MAXTRIES;
	
    } else {
	ntries = 1;
	ports[0] = port;
    }
    for (i = 0; i < ntries; i++) {
	sprintf(sport, "%d", ports[i]);
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_flags |= AI_NUMERICSERV;
	hints.ai_flags |= AI_ADDRCONFIG;
	if (getaddrinfo(NULL, sport, &hints, &listp) != 0) {
	    err(false, "Couldn't call getaddrinfo");
	    return false; 
	}
	for (p = listp; p; p = p->ai_next) {
	    if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
		continue;
	    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
			   (const void *) &optval, sizeof(int)) < 0) {
		close(listenfd);
		continue;
	    }
	    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
		if (listen(listenfd, 1024) >= 0)
		    break;
	    close(listenfd);
	}
	freeaddrinfo(listp);
	if (p) {
	    if (fdp)
		*fdp = listenfd;
	    if (portp)
		*portp = ports[i];
	    return true;
	}
    }
    err(false, "Failed %d tries to set up server", ntries);
    return false;
}

/* Open connection to server.  Return socket file descriptor */
int open_clientfd(char *hostname, unsigned port) {
    int clientfd;
    struct addrinfo hints, *listp, *p;
    char sport[10];
    sprintf(sport, "%d", port);

    /* Get list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;
    if (getaddrinfo(hostname, sport, &hints, &listp) != 0) {
	err(false, "Couldn't call getaddrinfo");
	return -1; /* Check errno for cause of error */
    }

    /* Walk the list until obtain successful connection */
    for (p = listp; p; p = p->ai_next) {
	if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
	    continue; /* Socket failed */
	if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
	    break; /* Success */
	if (close(clientfd) != 0) {
	    return -1;
	}
    }
    freeaddrinfo(listp);
    if (p) {
	report(4, "Opened connection to  %s:%s.", hostname, sport);
	return clientfd;
    } else {
	err(false, "Couldn't open connection to %s:%s", hostname, sport);
	return -1;
    }
}

/* Open connection to server based on IPv4 addrss.
   Return socket file descriptor */
int open_clientfd_ip(unsigned ip, unsigned port) {
    char sip[INET_ADDRSTRLEN];
    struct in_addr ina;
    /* Convert ip to dotted decimal form */
    ina.s_addr = htonl(ip);
    char *name = inet_ntop(AF_INET, &ina, sip, INET_ADDRSTRLEN);
    return open_clientfd(name, port);
}


/* Accept a connection request from a client
   Return connection socket descriptor.
   (Optionally) update pointers to IP address */
int accept_connection(int listenfd, unsigned *ipp) {
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    char hostname[INET_ADDRSTRLEN];
    char sport[10];
    
    int clientfd = accept(listenfd, (struct sockaddr *) &clientaddr, &clientlen);
    if (clientfd < 0) {
	err(false, "Accept failed");
	return clientfd;
    }
    if (getnameinfo((struct sockaddr *) &clientaddr, clientlen,
		    hostname, INET_ADDRSTRLEN,
		    sport, 10, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
	report(3, "Accepted connection from %s:%s", hostname, sport);
    }
    if (ipp) {
	struct sockaddr_in *cap = (struct sockaddr_in *) &clientaddr;
	*ipp = ntohl(cap->sin_addr.s_addr);
    }
    return clientfd;
}
