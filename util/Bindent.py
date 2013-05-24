#!/bin/python

import sys
import os

DEFAULT_INDENT = "\t"

def indent_from_arg(indarg):
	type = indarg[-1]
	indarg = indarg[:-1]
	count = int(indarg)
	if type == 's':
		return ' ' * count
	elif type == 't':
		return "\t" * count
	else:
		return "\t" * count


def indent_from_buffer():
	if "TEPID" not in os.environ: return DEFAULT_INDENT
	if "BUFID" not in os.environ: return DEFAULT_INDENT

	path = "/tmp/teddy." + os.environ["TEPID"] + "/" + os.environ["BUFID"] + "/prop/indentchar"

	try:
		with open(path) as f:
			return f.read()
	except:
		return DEFAULT_INDENT

def getindent():
	return indent_from_arg(sys.argv[2]) if len(sys.argv) >= 3 else indent_from_buffer()

def indent(indchar):
	for line in sys.stdin:
		sys.stdout.write(indchar + line)

def deindent(indchar):
	for line in sys.stdin:
		if line.startswith(indchar):
			sys.stdout.write(line[len(indchar):])
		elif line.startswith(" ") or line.startswith("\t"):
			sys.stdout.write(line[1:])
		else:
			sys.stdout.write(line)

def guessindent_intl():
	path = "/tmp/teddy." + os.environ["TEPID"] + "/" + os.environ["BUFID"] + "/body"
	try:
		with open(path) as f:
			count = 0
			spaces = 0
			tabs = 0
			for line in f:
				count = count + 1
				if count > 200:
					break
				if line[0] == " ":
					spaces = spaces + 1
				elif line[0] == "\t":
					tabs = tabs + 1
		#print "spaces", spaces, "tabs", tabs
		if spaces > tabs:
			return "  "
		else:
			return "\t"
	except:
		return " "

def guessindent():
	indchar = guessindent_intl()
	path = "/tmp/teddy." + os.environ["TEPID"] + "/" + os.environ["BUFID"] + "/prop/indentchar"
	try:
		with open(path, "w") as f:
			f.write(indchar)

	except:
		pass


if sys.argv[1] == "+":
	indent(getindent())
elif sys.argv[1] == "-":
	deindent(getindent())
elif sys.argv[1] == "guess":
	guessindent()
