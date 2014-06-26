/* Primitives for supporting dataflow execution */

/* Constants */
/* Default port for controller */
#define CPORT 6616


/* Enumeration of different message types */
enum {
    MSG_OPERATION,          /* Dataflow operation */
    MSG_OPERAND,            /* Dataflow operand */
    /* From client, router, or worker to controller */
    MSG_REGISTER_ROUTER,
    MSG_REGISTER_CLIENT,
    MSG_REGISTER_WORKER,
    /* From controller to client or worker */
    MSG_ACK_AGENT,
    /* From client or worker to router */
    MSG_REGISTER_AGENT,
    /* From worker back to controller */
    MSG_READY_WORKER,
    MSG_STAT,
    /* From controller to router, worker, or client */
    MSG_DO_FLUSH,
    MSG_KILL,
    /* Negative acknowledgement */
    MSG_NACK,
    /* Global operations */
    /* Initiated by client */
    MSG_CLIOP_DATA,
    MSG_CLIOP_ACK,
    /* Intiated by controller */
    MSG_GC_REQUEST,
    MSG_GC_START,
    MSG_GC_FINISH
};

/**********************************************************
 Fields and formats

Basic units (all sizes given in bytes)

Agent: 2
Message sequence number: 2+
Operation opcode: 1
Message code: 2
Port: 2
IP Address: 4
** Format of all messages **

Most messages have single-word headers.  Only operation and operands have two-word headers.

Single-word header formats

Word count: 2
Worker count: 2
Generation count: 4

Composite quantities.  All listed from MSB to least:

Operator ID:  4.  Combines Agent + Sequence Number

Operand ID:  5.  Operator ID (4) + Offset (1)

Node ID:     6.  Port (2) + IP Address (4)
Agent Map:   8.  Agent (2) + Node ID (6)

*** Double word header formats ***


**********************************************************/
/** Constants **/

/* Operator header has three words: two for control information and one for valid mask */
#define OP_HEADER_CNT 3
/* Use of bit vector for valid mask limits maximum operator length */
#define OP_MAX_LENGTH WORD_BITS

/* Operand header has two words for control information */
#define OPER_HEADER_CNT 2

/* Size of operand */
#define OPER_SIZE 2

/** Constructors **/

/* Create an operand destination */
dword_t msg_build_destination(unsigned agent, word_t operator_id, unsigned offset);

/* Create IP address from port and host */
word_t msg_build_node_id(unsigned port, unsigned ip);

/** Extractors **/

bool msg_is_client_agent(unsigned agent);

/* For single-word headers */
unsigned msg_get_header_code(word_t header);
unsigned msg_get_header_agent(word_t header);
unsigned msg_get_header_opcode(word_t header);
unsigned msg_get_header_offset(word_t header);

unsigned msg_get_header_port(word_t header);
unsigned msg_get_header_ip(word_t header);

unsigned msg_get_header_wordcount(word_t header);
unsigned msg_get_header_workercount(word_t header);
unsigned msg_get_header_generation(word_t header);

/* For double-word headers */

unsigned msg_get_dheader_code(dword_t header);
unsigned msg_get_dheader_agent(dword_t header);
word_t   msg_get_dheader_op_id(dword_t header);
unsigned msg_get_dheader_opcode(dword_t header);
unsigned msg_get_dheader_offset(dword_t header);

/* Extracting information from destination (double word) */
unsigned msg_get_dest_agent(dword_t dest);
word_t    msg_get_dest_op_id(dword_t dest);
unsigned msg_get_dest_offset(dword_t dest);

/** Message builders **/

/* Create an empty operator */
/* len specifies total message size, including header */
chunk_ptr msg_new_operator(unsigned opcode, unsigned agent, word_t operator_id, unsigned len);

/* Create destination from operator.  Offset includes header size */
dword_t msg_new_destination(chunk_ptr operator, unsigned offset);

/* Create empty operand.  len specifies total message size, including header */
chunk_ptr msg_new_operand(dword_t dest, unsigned len);

/* Create message to register client, router, worker, or agent */
chunk_ptr msg_new_register_router(unsigned port);
chunk_ptr msg_new_register_client();
chunk_ptr msg_new_register_worker();
chunk_ptr msg_new_register_agent(unsigned agent);
chunk_ptr msg_new_nack();

/* Create message to notify controller that worker is ready */
chunk_ptr msg_new_worker_ready(unsigned agent);

/* Create message to notify any node that it should terminate */
chunk_ptr msg_new_kill();
/* Create message to notify any node that it should flush its state */
chunk_ptr msg_new_flush();

/*
  Create a message containing worker statistics.
  Specify number of workers, number of values and provide pointer to array of values.
 */
chunk_ptr msg_new_stat(unsigned nworker, unsigned nstat, size_t *vals);

/*** Unary operations ***/

/* Create message containing global operation data */
/* nwords specifies number of data words (not including header) */
chunk_ptr msg_new_cliop_data(unsigned agent, unsigned opcode, unsigned nword, word_t *data);

chunk_ptr msg_new_cliop_ack(unsigned agent);
chunk_ptr msg_new_gc_request(unsigned gen);
chunk_ptr msg_new_gc_start();
chunk_ptr msg_new_gc_finish();


/** Useful functions **/

/* Create listening socket.
   Port value of 0 indicates that port can be chosen arbitrarily.
   If successful, set fdp to fd for listening socket and portp to port.
 */

bool new_server(unsigned port, int *fdp, unsigned *portp);

/* Open connection to server.  Return socket file descriptor
 *   Returns -1 and sets errno on Unix error. 
 *   Returns -2 and sets h_errno on DNS (gethostbyname) error.
 */
int open_clientfd(char *hostname, unsigned port);

/* Open connection to server given IPv4 host address.  Return socket file descriptor
 *   Returns -1 and sets errno on Unix error. 
 *   Returns -2 and sets h_errno on DNS (gethostbyname) error.
 */
int open_clientfd_ip(unsigned ip, unsigned port);

/* Accept a connection request from a client */
/* Return connection socket descriptor.  (Optionally) update pointer to IP address */
int accept_connection(int listenfd, unsigned *ipp);
