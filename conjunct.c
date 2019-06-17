/* Support for conjunction and Coudert/Madre simplification operations */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/select.h>
#include <signal.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"

#include "msg.h"
#include "console.h"
#include "agent.h"
#include "bdd.h"
#include "cudd.h"
#include "shadow.h"
#include "report.h"
#include "conjunct.h"


/* Parameters */

/* Should I check results of conjunct (and possibly other) operations? */
int check_results = 0;



/* Representation of set of terms to be conjuncted */

#define MAXTERM 1024

struct {
    ref_t fun;
    double sat_count;
    size_t size;
} terms[MAXTERM];

size_t term_count = 0;


void init_conjunct() {

    /* Add commands and options */
    add_param("check", &check_results, "Check results of conjunct (and other?) operations", NULL);
    term_count = 0;

}

void conjunct_add_term(ref_t fun) {
    root_addref(fun, false);
    terms[term_count++].fun = fun;
}

/* Compute AND of terms without decrementing reference counts */
static ref_t and_check() {
    int i;
    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    for (i = 0; i < term_count; i++) {
	ref_t arg = terms[i].fun;
	ref_t nval = shadow_and(smgr, rval, arg);
	root_deref(rval);
	rval = nval;
    }
    return rval;
}

void clear_conjunction() {
    int i;
    for (i = 0; i < term_count; i++) {
	ref_t arg = terms[i].fun;
	root_deref(arg);
    }
    term_count = 0;
}

/* Compute conjunction of terms.  Reset term set to empty */
ref_t compute_conjunction() {
    int i;
    
    ref_t rprod = check_results ? and_check() : REF_INVALID;

    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    for (i = 0; i < term_count; i++) {
	ref_t arg = terms[i].fun;
	ref_t nval = shadow_and(smgr, rval, arg);
	root_addref(nval, true);
	root_deref(rval);
	rval = nval;
    }
    clear_conjunction();

    if (check_results) {
	if (rprod != rval) {
	    char prod_buf[24], conj_buf[24];
	    shadow_show(smgr, rprod, prod_buf);
	    shadow_show(smgr, rval, conj_buf);
	    report(0, "WARNING: Conjuncting (%s) != Product (%s)", conj_buf, prod_buf);
	}
	root_deref(rprod);
    }
    return rval;
}
