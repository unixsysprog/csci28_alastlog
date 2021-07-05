# ----------------------------------------------------------
# This is a makefile.  A makefile contains rules that tell
# how to build a program, often from separate source files
#
# This sample makefile shows how to build dumputmp from
# the two source files dumputmp.c and utmplib.c
#
# You must modify this file so it contains rules to make
# your solution to the project
# (note: the indented lines MUST start with a single tab
#

GCC = gcc -Wall -Wextra -g

alastlog: alastlog.o lllib.o
	$(GCC) -o alastlog alastlog.o lllib.o

alastlog.o: alastlog.c
	$(GCC) -c alastlog.c

lllib.o: lllib.c
	$(GCC) -c lllib.c

clean:
	rm -f *.o alastlog

