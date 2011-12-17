#!/usr/bin/env perl
use warnings;
use strict;

sub trim {
    local $_ = shift;
    s/^\s+//g;
    s/\s+$//g;
    return $_;
}

print "#include \"colors.h\"\n";
print "#include \"global.h\"\n";

print "void init_colors(void) {\n";
print "   x11colors = g_hash_table_new(g_str_hash, streq);\n";

open my $in, '<', 'rgb.txt' or die "Couldn't open rgb.txt: $!";
while (<$in>) {
    chomp;
    next if $_ eq "";
    next if /^!/;
    $_ = trim($_);
    my ($r, $g, $b, $name) = split /\s+/, $_, 4;
    my $value = $b + ($r << 8) + ($g << 16);
    print "   g_hash_table_replace(x11colors, \"$name\", (gpointer)$value);\n";
}
close $in;

print "}\n\n";
