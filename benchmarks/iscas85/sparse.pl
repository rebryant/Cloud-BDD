#!/usr/bin/perl

use Getopt::Std;

# Split up grep results for CUDD sizes

# Typical entry:

# c432-ar-ca.out:  Cudd size: 31321 nodes

# Convert to:

# c432 r a a 31321


getopts('h:');

if ($opt_h) {
    print STDERR "Usage $ARGV[0] [-h]\n";
}

$lastkey = "";

while (<>) {
    $line = $_;
    chomp $line;
    @fields = split(/[- :\/\t]+/, $line);
    $bench = $fields[0];
    @tfields = split "", $fields[1];
    $dd = $tfields[0];
    $order = $tfields[1];
    @cfields = split "", $fields[2];
    $chain = $cfields[1];
    $l2field = $fields[$fields-2];
    $l1field = $fields[$fields -1];
    $lfield = "$l2field$l1field";
    $lfield =~ s/[=a-zA-Z]//g;
    print "$bench\t$order\t$dd\t$chain\t$lfield\n";
}

