/* Implementation of client or worker in dataflow system */

/* Counters tracked by agent */
enum {STATA_BYTE_PEAK, STATA_OPERATION_TOTAL,
      STATA_OPERATION_LOCAL, STATA_OPERAND_TOTAL,
      STATA_OPERAND_LOCAL, NSTATA};

/* Array of counters for accumulating statistics */
size_t agent_stat_counter[NSTATA];

/* What's my agent ID? */
unsigned own_agent;

void init_agent(bool iscli, char *controller_name, unsigned controller_port);

/* Function to implement specific operation */
typedef bool (*op_handler)(chunk_ptr args);

/* Add handler for operation */
void add_op_handler(unsigned opcode, op_handler h);

/* Print statistics about agent */
void agent_show_stat();

/* Create a new operator id */
unsigned new_operator_id();

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
  Worker agent modified to within bounds.
  Return true if successful.
*/
bool send_op(chunk_ptr msg);

/* Send single-valued operand */
bool send_as_operand(word_t dest, word_t val);

/* Insert an operand into an operation */
void insert_operand(chunk_ptr op, chunk_ptr oper, unsigned offset);

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

/* Fire an operation and wait for returned operand */
chunk_ptr fire_and_wait(chunk_ptr msg);

void run_client(char *infile_name);

void run_worker();

