This directory contains files for constructing a distributed BDD package.

The only external code used is for the CUDD BDD package

All code is in C, using Unix sockets as the communication mechanism.

Files:

agent.{c,h}:
    Support worker and client agents executing dynamic data-flow

bdd.{c,h}:
    BDD implementation based on "refs".  Support for both single-process
    execution, as well as for data-flow based distributed execution

bworker.c:
    Worker agent for BDD execution

chunk.{c,h}:
chunk_test.c
    Data structure for dynamically allocated, variable-length arrays
    Underlying data structure for most data blocks, and for data-flow messages

console.{c,h}:
console_test.c
    Support for command-line interfaces, used by both controller and
    clients.

dtype.h:
    Declarations of widely used data types

msg.{c,h}:
    Support for messages and routing in data-flow execution model

report.{c,h}:
    Utilities for reporting events at different levels of verbosity
    and for allocating/freeing memory in trackable way

router.c:
    Router to support communication between data-flow agents

runbdd.c:
    Can run as standalone BDD manipulator, as well as client for
    distributed execution.  Supports evaluation by CUDD, by
    single-processor, and by distributed implementation

shadow.{c,h}:
shadow_test.c
    Common API to Boolean manipulation primitives, allowing any
    combination of evaluation methods, and checking consistency among them.

table.{c,h}:
chunktable_test.c
set_test.c
    Support for dictionaries and sets

tclient.c
tworker.c:
test_df.{h,c}
    Testing framework for data-flow execution



    

