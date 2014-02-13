/* Data flow operators to implement testing programs */

/********************************************************
 Supported operations:

 ifork(dest, width, val, cnt):
    If width == 1, then invoke incr(dest, val, cnt)
    Else create destinations dest1, dest2
      join(dest, dest1, dest2)
      incr(dest1, width/2, val, cnt)
      incr(dest2, width-width/2, val, cnt)
 incr(dest, val, cnt):
    If cnt == 0, send val to dest
    Else invoke incr(dest, val+1, cnt-1)

 join(dest, val1, val2)
    Send val1+val2 to dest

 sink(val):
    Notify agent (must be client) that have value val


********************************************************/

typedef enum { OP_IFORK, OP_INCR, OP_JOIN } opcode_t;

#define NSTAT NSTATA
void do_summary_stat(chunk_ptr smsg);

chunk_ptr build_ifork(word_t dest, word_t width, word_t val, word_t cnt);
chunk_ptr build_incr(word_t dest, word_t val, word_t cnt);
chunk_ptr build_join(word_t dest);

bool do_ifork_op(chunk_ptr op);
bool do_incr_op(chunk_ptr op);
bool do_join_op(chunk_ptr op);

chunk_ptr flush_worker();

/* Global operations by workers */
void start_global(unsigned id, unsigned opcode, unsigned nword, word_t *data);
void finish_global(unsigned id);
