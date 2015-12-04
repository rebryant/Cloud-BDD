#!/usr/bin/perl

# Implement remote kill of specified application

use Getopt::Std;

# Name of application
$app = "controller";

getopts('ha:');

$thisprog = $0;

if ($opt_h) {
    print STDERR "Usage: $0 [-h] [-a application]\n";
    exit(0);
}

if ($opt_a) {
    $app = $opt_a;
}

$processes = `ps -e` || die "Couldn't get list of processes\n";

@plist = split("\n", $processes);

for $p (@plist) {
    chomp $p;
    if ($p =~ $app && !($p =~ $thisprog)) {
	if ($p =~ /^[\s]*([\d]+)/) {
	    $pid = $1;
	    print "Killing process $pid\n";
	    system "kill -15 $pid" || die "Couldn't kill process $pid\n";
	}
    }
}
