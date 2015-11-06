#!/usr/bin/perl

use Getopt::Std;

# Directory for all installation scripts
$idir = "Cloud-BDD/aws";

# Controller host
$controller_host = "whaleshark.ics.cs.cmu.edu";

# Controller port
$controller_port = 6616;

getopts('hH:P:d:');

if ($opt_h) {
    print STDERR "Usage: $0 [-h] [-H host] [-P port] [-d scriptdir]";
    exit(0);
}

if ($opt_H) {
    $controller_host = $opt_H;
}

if ($opt_P) {
    $controller_port = $opt_P;
}

if ($opt_d) {
    $idir = $opt_d;
}


# Make sure everything has been installed
system "perl $idir/install.pl -d $idir" || die "Couldn't run installation script\n";

# Fire up router
system "./Cloud-BDD/router -H $controller_host -P $controller_port" || die "Couldn't run router\n";




