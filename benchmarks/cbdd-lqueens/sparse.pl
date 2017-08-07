#!/usr/bin/perl

use Getopt::Std;

# Split up grep results for CUDD sizes

# Typical entry:

# q08/lqb08-bin-fast-q-cn.out:  Cudd size: 877 nodes

# Convert to:

# 08 b bin cn 1055

# Optionally allow labels within single file:
$labcnt = 0;    
@labels = ();



getopts('hl:');

if ($opt_h) {
    print STDERR "Usage $ARGV[0] [-h] [-l lab1:lab2:....:labk]\n";
}

if ($opt_l) {
    @labels = split(":", $opt_l);
    $labcnt = @labels;
}

$idx = 0;

$lastkey = "";

while (<>) {
    $line = $_;
    chomp $line;
    @fields = split(/[- :\/\t]+/, $line);
#    $txt = join("+", @fields);
#    print "Fields = $txt\n";
    $n = $fields[0];
    $n =~ s/[qz]//g;
    @mstring = split("", $fields[1]);
    $mode = $mstring[2];
    $encode = $fields[2];
    @cstring = split(/\./, $fields[5]);
    $chain = $cstring[0];
    $l2field = $fields[$fields-2];
    $l1field = $fields[$fields -1];
    $lfield = "$l2field$l1field";
    $lfield =~ s/[=a-zA-Z]//g;
    $key = "$mode+$encode+$chain";
    if (!($key eq $lastkey)) {
	$idx = 0;
    }
    $lastkey = $key;
    $labstring = "";
    if ($labcnt > 0) {
	$lab = "";
	if ($idx < $labcnt) {
	    $lab = $labels[$idx];
	}
	$idx += 1;
	$labstring = "\t$lab";
    }
    print "$n\t$mode\t$encode\t$chain$labstring\t$lfield\n";
}

