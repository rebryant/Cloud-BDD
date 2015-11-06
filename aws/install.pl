#!/usr/bin/perl

use Getopt::Std;

# Directory for all installation scripts
$idir = "Cloud-BDD/aws";

getopts('hd:');

if ($opt_h) {
    print STDERR "Usage: $0 [-h] [-d scriptdir]";
    exit(0);
}

if ($opt_d) {
    $idir = $opt_d;
}


if (!(-e "/usr/bin/git")) {
    system "sudo yum -y install git" || die "Couldn't install git\n";
}

# Get fresh copy of code, including rest of installation code
if (!(-e "./Cloud-BDD")) {
    system "git clone https://github.com/rebryant/Cloud-BDD.git" || die "Couldn't fetch cloud BDD\n";
} else {
    system "pushd Cloud-BDD; git pull; popd" || die "Couldn't update cloud BDD\n";
}

# Now do rest of installation (perhaps with updated program)
system "perl $idir/install-rest.pl" || die "Couldn't run rest of installation script\n";

