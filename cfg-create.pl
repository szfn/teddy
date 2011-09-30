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
}

close $in;

@cfg_names = sort { $a cmp $b } keys %cfgs;

my $config_count = scalar @cfg_names;

open my $hout, '>', 'cfg.h' or die "Can't write cfg.h: $!";

print $hout <<"HHEAD";
#ifndef __CFG__
#define __CFG__

#define CONFIG_ITEM_STRING_SIZE 512

typedef struct _config_item_t {
	char strval[CONFIG_ITEM_STRING_SIZE];
	int intval;
} config_item_t;

extern config_item_t config[];
extern const char *config_names[];

extern void cfg_init(void);
void setcfg(config_item_t *ci, const char *val);

#define CONFIG_NUM $config_count

HHEAD

for my $i (0 .. $#cfg_names) {
	print $hout "#define ".uc($cfg_names[$i])." $i\n";
}

print $hout "\n#endif\n";

close $hout;

open my $cout, '>', 'cfg.c' or die "Can't write cfg.c: $!";

print $cout <<"CHEAD";
#include "cfg.h"

#include <string.h>
#include <stdlib.h>

config_item_t config[$config_count];
const char *config_names[] = {
CHEAD

for my $cfg_name (@cfg_names) {
	my $clean_cfg_name = $cfg_name;
	$clean_cfg_name =~ s/^cfg_//g;
	print $cout "\t\"$clean_cfg_name\",\n";
}

print $cout <<"CBODY";
};

void setcfg(config_item_t *ci, const char *val) {
    strcpy(ci->strval, val);
    ci->intval = atoi(val);
}

void cfg_init(void) {
CBODY

for my $cfg_name (@cfg_names) {
	print $cout "\tsetcfg(config + " . uc($cfg_name) . ", \"" . $cfgs{$cfg_name} . "\");\n";
}

print $cout <<"CBODY_END";
}
CBODY_END

#TODO:
# - print initialization function