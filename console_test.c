/* Test out console API */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/select.h>

#include "report.h"
#include "console.h"



/* Implement simple-minded calculator */

int value = 0;

bool app_quit(int argc, char *argv[]) {
    report(0, "Quitting application.  Value = %d", value);
    return true;
}

bool do_times(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
	int oldval = value;
	int arg;
	if (!get_int(argv[i], &arg)) {
	    report(0, "Couldn't parse '%s' as int", argv[i]);
	    return false;
	}
	value *= arg;
	report(0, "%d * %d --> %d", oldval, arg, value);
    }
    return true;
}

bool do_plus(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
	int oldval = value;
	int arg;
	if (!get_int(argv[i], &arg)) {
	    report(0, "Couldn't parse '%s' as int", argv[i]);
	    return false;
	}
	value += arg;
	report(0, "%d + %d --> %d", oldval, arg, value);
    }
    return true;
}


int main(int argc, char *argv[]) {
    init_cmd();
    add_cmd("times", do_times, "Multiply");
    add_cmd("plus", do_plus, "Add");
    add_param("value", &value, "Value");
    add_quit_helper(app_quit);
    run_console(NULL);
    finish_cmd();
    return 0;
}
