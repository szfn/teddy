#!/usr/bin/env perl
use warnings;
use strict;

open my $out, '>', "docs.c" or die "Couldn't output to docs.c: $!";

sub output_text {
	my ($filename, $varname) = @_;

	open my $in, '<', $filename or die "Couldn't open $filename: $!";

	print $out "const unsigned char ${varname}[] = \"";

	while (<$in>) {
		chomp;
		s:\\:\\\\:g;
		s:":\\":g;
		print $out "$_\\n\\\n";
	}

	print $out "\";\n";

	print $out "size_t ${varname}_size = sizeof(${varname}) / sizeof(unsigned char) - 1;\n";

	close $in;
}

sub output_bytes {
	my ($filename, $varname) = @_;

	open my $in, '<', $filename or die "Couldn't open $filename: $!";

	print $out "const unsigned char ${varname}[] = {";

	my $count = 0;
	my ($data, $n);
	while (($n = read $in, $data, 1) != 0) {
		printf $out "0x%02x, ", ord($data);
		print $out "\n" if ($count++ % 20) == 0;
	}

	print $out "};\n";

	print $out "size_t ${varname}_size = sizeof(${varname}) / sizeof(unsigned char);\n";

	close $in;
}

print $out "#include <stdlib.h>\n";

output_text("doc/index.html", "index_doc");
output_text("doc/commands.html", "commands_doc");
output_text("doc/keyboard.html", "keyboard_doc");
output_text("doc/mouse.html", "mouse_doc");
output_bytes("doc/teddy_frame.png", "teddy_frame_png");
output_bytes("doc/teddy_link.png", "teddy_link_png");
output_bytes("doc/teddy_window.png", "teddy_window_png");

close $out;
