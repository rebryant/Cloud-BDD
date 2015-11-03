/* Implementation of client or worker in dataflow system */

/* Counters tracked by agent */
enum {STATA_BYTE_PEAK, STATA_OPERATION_TOTAL,
      STATA_OPERATION_LOCAL, STATA_OPERAND_TOTAL,
      STATA_OPERAND_LOCAL, NSTATA};

/* Array of counters for accumulating statistics */
size_t agent_stat_counter[NSTATA];

/* What's my agent ID? */
unsigned own_agent;

/* Initialize agent.  Optionally specify that should attempt to use a local router */
void init_agent(bool iscli, char *controller_name, unsigned controller_port,
		bool try_local_route);

/* Function to implement specific operation */
typedef bool (*op_handler)(chunk_ptr args);

/* Add handler for operation */
void add_op_handler(unsigned opcode, op_handler h);

/* Print statistics about agent */
void agent_show_stat();

/* Create a new operator id */
word_t new_operator_id();

/* Get agent ID for worker based on some hashed value */
unsigned choose_hashed_worker(word_t hash);


/* Get agent ID for arbitary worker (Policy determines how chosen) */
unsigned choose_some_worker();

/* Get agent ID for random worker */
unsigned choose_random_worker();

/* Get agent ID for local worker */
unsigned choose_own_worker();


/*
  Send chunk containing either operator or operand.
  Message contains its own routing information.
  Return true if successful.
*/
bool send_op(chunk_ptr msg);

/* Send single-valued operand */
bool send_as_operand(dword_t dest, word_t val);

/* Insert word into operator, updating its valid mask.
   Offset includes header size */
void op_insert_word(chunk_ptr op, word_t wd, size_t offset);

/* Insert double word into operator, updating its valid mask.
   Offset includes header size */
/* Offset is for first word */
void op_insert_dword(chunk_ptr op, dword_t wd, size_t offset);

/*
  Insert an operand into an operation.
  Updates its valid mask.  Offset includes header size
 */
void op_insert_operand(chunk_ptr op, chunk_ptr oper, unsigned offset);

/* Check whether all fields an an operator are valid */
bool op_check_full(chunk_ptr op);

/*
  Function to flush state and return message with accumulated statistics.
  Function returns NULL when not statistics to provide 
*/
typedef chunk_ptr (*flush_function)();

/* Provide handler to support flush operation. */
void set_agent_flush_helper(flush_function ff);

/*
  Function to receive summary statistics message.
  For each value, have min, max, and sum in sequence.
*/
typedef void (*stat_function)(chunk_ptr smsg);

/* Provide handler to support flush operation. */
void set_agent_stat_helper(stat_function ff);

/* Fire an operation and wait for returned operand.  Starts any deferred GC */
chunk_ptr fire_and_wait(chunk_ptr msg);

/* Fire an operation and wait for returned operand.
   Does not start any deferred GC */
chunk_ptr fire_and_wait_defer(chunk_ptr msg);

/* Enable a deferred garbage collection */
void undefer();

/* Function to perform GC on either client or worker */
typedef void (*gc_handler)();

void set_gc_handlers(gc_handler start_handler, gc_handler finish_handler);

/* Function for requesting a GC by a worker */
void request_gc();

void run_client(char *infile_name);

void run_worker();



/* Functions to handle global operations */
/* Start function includes opcode to specify operation
   + application specific data */
/* This function should NOT deallocate the array indicated by argument data */
typedef void (*global_op_start_function)
(unsigned id, unsigned opcode, unsigned nword, word_t *data);
/* Finish function */
typedef void (*global_op_finish_function)(unsigned id);

/* Provide handlers to perform global operation by worker */
void set_agent_global_helpers(global_op_start_function gosf,
			      global_op_finish_function goff);

/*
  Initiate global operation from client.
  Returns to client when all workers ready to perform their operations 
*/
bool start_client_global(unsigned opcode, unsigned nword, word_t *data);

/*
  Finalize global operation from client.
*/
bool finish_client_global();






