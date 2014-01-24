/* Implementation of client or worker in dataflow system */

/* What's my agent ID? */
unsigned own_agent;

void init_agent(bool iscli, char *controller_name, unsigned controller_port);

/* Function to implement specific operation */
typedef bool (*op_handler)(chunk_ptr args);

/* Add handler for operation */
void add_op_handler(unsigned opcode, op_handler h);

/* Create a new operator id */
unsigned new_operator_id();

/* Get agent ID for worker based on some hashed value */
unsigned choose_hashed_worker(word_t hash);

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

/* Set function to be called when agent command to flush its state */
void set_agent_flush_helper(cmd_function ff);

/* Fire an operation and wait for returned operand */
chunk_ptr fire_and_wait(chunk_ptr msg);

void run_client(char *infile_name);

void run_worker();

