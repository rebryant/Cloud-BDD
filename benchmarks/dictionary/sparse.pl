#!/usr/bin/perl

use Getopt::Std;

# Split up grep results for CUDD sizes

# Typical entry:

# source-passwords/source-passwords-bba.out:  Cudd size: 1007868 nodes

# Convert to:

# source-passwords b b a 1007868


getopts('h:');

if ($opt_h) {
    print STDERR "Usage $ARGV[0] [-h]\n";
}

$lastkey = "";

while (<>) {
    $line = $_;
    chomp $line;
    @fields = split(/[ :\/\t]+/, $line);
#    $txt = join("+", @fields);
#    print "Fields = $txt\n";
    $bench = $fields[0];
    @subfields = split(/-/, $fields[1]);
    $mstring = $subfields[$subfields-1];
    @mode = split("", $mstring);
    $encode = $mode[0];
    $dd = $mode[1];
    $chain = $mode[2];
    $l2field = $fields[$fields-2];
    $l1field = $fields[$fields -1];
    $lfield = "$l2field$l1field";
    $lfield =~ s/[=a-zA-Z]//g;
    print "$n\t$bench\t$encode\t$dd\t$chain\t$lfield\n";
}

