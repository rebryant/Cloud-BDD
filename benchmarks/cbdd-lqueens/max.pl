#!/usr/bin/perl

# Extract maximum value for each key from key/value pairs

$keyidx = 0;
$validx = 3;

$nokey = "No Key";

$lastkey = $nokey;
$maxval = 0;
$maxline = "";

while (<>) {
    $line = $_;
    chomp $line;
    @fields = split(/[\s]+/, $line);
    $key = $fields[$keyidx];
    $val = $fields[$validx];
    if ($key eq $lastkey) {
	if ($val > $maxval) {
	    $maxval = $val;
	    $maxline = $line;
	}
    } else {
	if (!($lastkey eq $nokey)) {
	    print "$maxline\n";
	}
	$lastkey = $key;
	$maxval = $val;
    }
}

if (!($lastkey eq $nokey)) {
    print "$maxline\n";
}
