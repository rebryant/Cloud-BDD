#!/usr/bin/perl

use Getopt::Std;

# Controller host
$controller_host = "whaleshark.ics.cs.cmu.edu";

# Controller port
$controller_port = 6616;

getopts('hH:P:');

if ($opt_h) {
    print STDERR "Usage: $0 [-h] [-H host] [-P port]";
    exit(0);
}

if ($opt_H) {
    $controller_host = $opt_H;
}

if ($opt_P) {
    $controller_port = $opt_P;
}


# Make sure everything has been installed

# Install standard packages
if (!(-e "/usr/bin/gcc")) {
    system "sudo yum -y install gcc" || die "Couldn't install gcc\n";
}

if (!(-e "/usr/bin/git")) {
    system "sudo yum -y install git" || die "Couldn't install git\n";
}

if (!(-e "./cudd")) {
    system "git clone https://github.com/johnyf/cudd.git" || die "Couldn't fetch CUDD\n";    
    system "pushd cudd/cudd; make; popd" || die "Couldn't install CUDD\n";
}

# Update BDD code
system "git clone https://github.com/rebryant/Cloud-BDD.git" || die "Couldn't fetch cloud BDD\n";
system "pushd Cloud-BDD; ln -s ../cudd/cudd cudd-symlink; make; popd" 
    || die "Couldn't compile code\n";
system "pushd Cloud-BDD/scripts; make; popd" 
    || die "Couldn't compile scripts\n";



# Fire up router
system "./Cloud-BDD/bworker -H $controller_host -P $controller_port" || die "Couldn't run router\n";




