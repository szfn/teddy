#!/bin/bash

if [ "x$1" = "x" ]; then
	echo "Usage: ./install.sh <directory>"
	exit
fi

cp util/g $1
chmod u+x $1/g
cp util/Bindent.py $1/Bindent
chmod u+x $1/Bindent
cp -f bin/teddy $1
