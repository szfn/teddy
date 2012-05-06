#!/bin/bash
#echo "#define GIT_COMPILATION_DATE \"teddy $(git show $(cat .git/HEAD | sed -e 's/ref: //g') | grep Date | sed -e 's/^Date:\s*//' | sed -e 's: :_:g')\"" > git.compilation.date.h
echo "#define GIT_COMPILATION_DATE \"teddy $(git show -s --format=format:%ad --date=iso $(cat .git/HEAD | sed -e 's/ref: //g')  | sed -e 's/-\|:\| //g' | sed -e 's/.......$//g')\"" > git.date.h