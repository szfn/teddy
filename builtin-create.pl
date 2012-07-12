#!/usr/bin/env perl
use warnings;
use strict;

sub create_header {
	my ($output_name, $input_name, $guard_name, $define_name) = @_;

	open my $out, '>', $output_name or die "Couldn't output $output_name: $!";
	open my $in, '<', $input_name or die "Couldn't read $input_name: $!";

	print $out "#ifndef $guard_name\n";
	print $out "#define $guard_name\n";

	print $out "\n#define $define_name \"";

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
}

create_header("builtin.h", "builtin.tcl", "__BUILTIN_TCL__", "BUILTIN_TCL_CODE");
create_header("autoconf.h", "example.teddy", "__AUTOCONF_TEDDY__", "AUTOCONF_TEDDY");
