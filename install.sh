#!/bin/bash

if [ "x$1" = "x" ]; then
	echo "Usage: ./install.sh <directory>"
	exit
fi

cp util/g $1
cp bin/teddy $1

