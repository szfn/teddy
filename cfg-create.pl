#!/usr/bin/env perl
use warnings;
use strict;

open my $in, '<', 'cfg.src' or die "Can't read cfg.src: $!";

my %cfgs = ();
my @cfg_names;

while (<$in>) {
	chomp;
	next if /^$/;
	my ($cfg_name, $cfg_init) = split / /, $_, 2;
	$cfgs{$cfg_name} = $cfg_init;
	push @cfg_names, $cfg_name;
}

close $in;

my $config_count = scalar @cfg_names;

open my $hout, '>', 'cfg_auto.h' or die "Can't write cfg_auto.h: $!";

print $hout <<"HHEAD";
#ifndef __CFG_AUTO__
#define __CFG_AUTO__

#define CONFIG_NUM $config_count

HHEAD

for my $i (0 .. $#cfg_names) {
	print $hout "#define ".uc($cfg_names[$i])." $i\n";
}

print $hout "\n#endif\n";

close $hout;

open my $cout, '>', 'cfg_auto.c' or die "Can't write cfg_auto.c: $!";

print $cout <<"CHEAD";
#include "cfg.h"
#include "cfg_auto.h"

#include <stdlib.h>

const char *config_names[] = {
CHEAD

for my $cfg_name (@cfg_names) {
	my $clean_cfg_name = $cfg_name;
	$clean_cfg_name =~ s/^cfg_//g;
	print $cout "\t\"$clean_cfg_name\",\n";
}

print $cout <<"CBODY";
};

void config_init_auto_defaults(void) {
\tconfig_init(&global_config, NULL);
CBODY

for my $cfg_name (@cfg_names) {
	print $cout "\tconfig_set(&global_config, " . uc($cfg_name) . ", \"" . $cfgs{$cfg_name} . "\");\n";
}

print $cout <<"CBODY_END";
}
CBODY_END

#TODO:
# - print initialization function