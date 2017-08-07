#!/usr/bin/perl

#Convert series of entries of form

#  N F1 F2 ... Fk V

#Into one where all the different V values are listed on a single row, with different values of N forming the columns

# Mapping from key+N to V
%vmap  = qw//;

%keyset = qw//;
%nset = qw//;

@keys = [];
$keycnt = 0;
@nvals = [];
$ncnt = 0;

while (<>) {
    $line = $_;
    chomp $line;
    @fields = split  "\t", $line;
    $n = $fields[0];
    $v = $fields[-1];
    splice @fields, 0, 1;
    splice @fields, -1, 1;
    $key = join("#", @fields);
    if (!($keyset{$key})) {
	$keys[$keycnt] = $key;
	$keycnt += 1;
	$keyset{$key} = 1;
    }
    if (!($nset{$n})) {
	$nvals[$ncnt] = $n;
	$ncnt += 1;
	$nset{$n} = 1;
    }
    $vmap{"$key:$n"} = $v;
}

if ($keycnt == 0) {
    print STDERR "No keys found\n";
    exit(0);
}

@kfields = split("#", $keys[0]);

for ($j = 0; $j < @kfields-1; $j += 1) {
    print "\t";
}

for ($j = 0; $j < $ncnt; $j+=1) {
    $n = $nvals[$j];
    print "\t$n";
}

print "\n";

for ($i = 0; $i < $keycnt; $i+=1) {
    $key = $keys[$i];
    @kfields = split("#", $key);
    print join("\t", @kfields);
    for ($j = 0; $j < $ncnt; $j+= 1) {
	$v = 0;
	$n = $nvals[$j];
	if ($vmap{"$key:$n"}) {
	    $v = $vmap{"$key:$n"};
	}
	print "\t$v";
    }
    print "\n";
}
