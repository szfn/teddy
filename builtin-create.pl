#!/usr/bin/env perl
use warnings;
use strict;

open my $out, '>', 'builtin.h' or die "Couldn't output builtin.h: $!";
open my $in, '<', 'builtin.tcl' or die "Couldn't read builtin.tcl: $!";

print $out "#ifndef __BUILTIN_TCL__\n";
print $out "#define __BUILTIN_TCL__\n";

print $out "\n#define BUILTIN_TCL_CODE \"";

while (<$in>) {
	chomp;
	s:\\:\\\\:g;
	s:":\\":g;
	print $out "$_\\n\\\n";
}

print $out "\"\n";

print $out "#endif\n";

close $in;
close $out;