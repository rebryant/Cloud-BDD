#!/usr/bin/perl

# Install standard packages
if (!(-e "/usr/bin/gcc")) {
    system "sudo yum -y install gcc" || die "Couldn't install gcc\n";
}

# Update CUDD
if (!(-e "./cudd")) {
    system "git clone https://github.com/johnyf/cudd.git" || die "Couldn't fetch CUDD\n";    
} else {
    system "pushd cudd; git pull; popd" || die "Couldn't update CUDD\n";    
}

system "pushd cudd/cudd; make -s; popd" || die "Couldn't compile CUDD\n";

if (!(-e "Cloud-BDD/cudd-symlink")) {
    system "pushd Cloud-BDD; ln -s ../cudd/cudd cudd-symlink; popd" 
	|| die "Couldn't create symbolic link\n";
}

system "pushd Cloud-BDD; make -s; popd" 
    || die "Couldn't compile cloud BDD\n";

system "pushd Cloud-BDD/scripts; make -s; popd" 
    || die "Couldn't compile scripts\n";







